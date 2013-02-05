/********************************************************************** 
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
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

#include <stdarg.h>

/* utility */
#include "bitvector.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "shared.h"
#include "support.h"

/* common */
#include "citizens.h"
#include "diptreaty.h"
#include "game.h"
#include "government.h"
#include "movement.h"
#include "packets.h"
#include "player.h"
#include "research.h"
#include "rgbcolor.h"
#include "tech.h"
#include "unitlist.h"

/* common/scriptcore */
#include "luascript_types.h"

/* server */
#include "aiiface.h"
#include "citytools.h"
#include "cityturn.h"
#include "connecthand.h"
#include "diplhand.h"
#include "gamehand.h"
#include "maphand.h"
#include "notify.h"
#include "plrhand.h"
#include "sernet.h"
#include "srv_main.h"
#include "stdinhand.h"
#include "spaceship.h"
#include "spacerace.h"
#include "techtools.h"
#include "unittools.h"
#include "voting.h"

/* server/advisors */
#include "advdata.h"
#include "autosettlers.h"

/* server/scripting */
#include "script_server.h"

struct rgbcolor;

static void package_player_common(struct player *plr,
                                  struct packet_player_info *packet);

static void package_player_diplstate(struct player *plr1,
                                     struct player *plr2,
                                     struct packet_player_diplstate *packet_ds,
                                     struct player *receiver,
                                     enum plr_info_level min_info_level);
static void package_player_info(struct player *plr,
                                struct packet_player_info *packet,
                                struct player *receiver,
                                enum plr_info_level min_info_level);
static enum plr_info_level player_info_level(struct player *plr,
					     struct player *receiver);

static void send_player_remove_info_c(const struct player_slot *pslot,
                                      struct conn_list *dest);
static void send_player_info_c_real(struct player *src,
                                    struct conn_list *dest);
static void send_player_diplstate_c_real(struct player *src,
                                         struct conn_list *dest);

/* Used by shuffle_players() and shuffled_player(). */
static int shuffled_order[MAX_NUM_PLAYER_SLOTS];

/**************************************************************************
  Murder a player in cold blood.

  Called from srv_main kill_dying_players() and edit packet handler
  handle_edit_player_remove().
**************************************************************************/
void kill_player(struct player *pplayer)
{
  bool palace;

  pplayer->is_alive = FALSE;

  /* reset player status */
  player_status_reset(pplayer);

  /* Remove shared vision from dead player to friends. */
  players_iterate(aplayer) {
    if (gives_shared_vision(pplayer, aplayer)) {
      remove_shared_vision(pplayer, aplayer);
    }
  } players_iterate_end;

  cancel_all_meetings(pplayer);

  /* Show entire map for players who are *not* in a team if revealmap is set
   * to REVEAL_MAP_DEAD. */
  if (game.server.revealmap & REVEAL_MAP_DEAD
      && player_list_size(team_members(pplayer->team)) == 1) {
    map_know_and_see_all(pplayer);
  }

  if (!is_barbarian(pplayer)) {
    notify_player(NULL, NULL, E_DESTROYED, ftc_server,
                  _("The %s are no more!"),
                  nation_plural_for_player(pplayer));
  }

  /* Transfer back all cities not originally owned by player to their
     rightful owners, if they are still around */
  palace = game.server.savepalace;
  game.server.savepalace = FALSE; /* moving it around is dumb */
  city_list_iterate(pplayer->cities, pcity) {
    if (pcity->original != pplayer && pcity->original->is_alive) {
      /* Transfer city to original owner, kill all its units outside of
         a radius of 3, give verbose messages of every unit transferred,
         and raze buildings according to raze chance (also removes palace) */
      transfer_city(pcity->original, pcity, 3, TRUE, TRUE, TRUE);
    }
  } city_list_iterate_end;

  /* Remove all units that are still ours */
  unit_list_iterate_safe(pplayer->units, punit) {
    wipe_unit(punit, ULR_PLAYER_DIED, NULL);
  } unit_list_iterate_safe_end;

  /* Destroy any remaining cities */
  city_list_iterate(pplayer->cities, pcity) {
    remove_city(pcity);
  } city_list_iterate_end;
  game.server.savepalace = palace;

  /* Remove ownership of tiles */
  whole_map_iterate(ptile) {
    if (tile_owner(ptile) == pplayer) {
      map_claim_ownership(ptile, NULL, NULL);
    }
  } whole_map_iterate_end;

  /* Ensure this dead player doesn't win with a spaceship.
   * Now that would be truly unbelievably dumb - Per */
  spaceship_init(&pplayer->spaceship);
  send_spaceship_info(pplayer, NULL);

  send_player_info_c(pplayer, game.est_connections);
}

/**************************************************************************
  Return player maxrate in legal range.
**************************************************************************/
static int get_player_maxrate(struct player *pplayer)
{
  int maxrate = get_player_bonus(pplayer, EFT_MAX_RATES);

  if (maxrate == 0) {
    return 100; /* effects not initialized yet */
  }

  /* 34 + 33 + 33 = 100 */
  return CLIP(34, maxrate, 100);
}

/**************************************************************************
  Handle a client or AI request to change the tax/luxury/science rates.
  This function does full sanity checking.
**************************************************************************/
void handle_player_rates(struct player *pplayer,
			 int tax, int luxury, int science)
{
  int maxrate;

  if (S_S_RUNNING != server_state()) {
    log_error("received player_rates packet from %s before start",
              player_name(pplayer));
    notify_player(pplayer, NULL, E_BAD_COMMAND, ftc_server,
                  _("Cannot change rates before game start."));
    return;
  }

  if (tax + luxury + science != 100) {
    return;
  }
  if (tax < 0 || tax > 100 || luxury < 0 || luxury > 100 || science < 0
      || science > 100) {
    return;
  }
  maxrate = get_player_maxrate(pplayer);
  if (tax > maxrate || luxury > maxrate || science > maxrate) {
    const char *rtype;

    if (tax > maxrate) {
      rtype = _("Tax");
    } else if (luxury > maxrate) {
      rtype = _("Luxury");
    } else {
      rtype = _("Science");
    }

    notify_player(pplayer, NULL, E_BAD_COMMAND, ftc_server,
                  _("%s rate exceeds the max rate for %s."),
                  rtype,
                  government_name_for_player(pplayer));
  } else {
    pplayer->economic.tax = tax;
    pplayer->economic.luxury = luxury;
    pplayer->economic.science = science;

    city_refresh_for_player(pplayer);
    send_player_info_c(pplayer, pplayer->connections);
  }
}

/**************************************************************************
  Finish the revolution and set the player's government.  Call this as soon
  as the player has set a target_government and the revolution_finishes
  turn has arrived.
**************************************************************************/
static void finish_revolution(struct player *pplayer)
{
  struct government *government = pplayer->target_government;

  fc_assert_ret(pplayer->target_government
                != game.government_during_revolution
                && NULL != pplayer->target_government);
  fc_assert_ret(pplayer->revolution_finishes <= game.info.turn);

  pplayer->government = government;
  pplayer->target_government = NULL;

  log_debug("Revolution finished for %s. Government is %s. "
            "Revofin %d (%d).", player_name(pplayer),
            government_rule_name(government),
            pplayer->revolution_finishes, game.info.turn);
  notify_player(pplayer, NULL, E_REVOLT_DONE, ftc_server,
                _("%s now governs the %s as a %s."), 
                player_name(pplayer), 
                nation_plural_for_player(pplayer),
                government_name_translation(government));

  if (!pplayer->ai_controlled) {
    /* Keep luxuries if we have any.  Try to max out science. -GJW */
    int max = get_player_maxrate(pplayer);

    /* only change rates if one exceeds the maximal rate */
    if (pplayer->economic.science > max || pplayer->economic.tax > max
        || pplayer->economic.luxury > max) {
      int save_science = pplayer->economic.science;
      int save_tax = pplayer->economic.tax;
      int save_luxury = pplayer->economic.luxury;

      pplayer->economic.science = MIN(100 - pplayer->economic.luxury, max);
      pplayer->economic.tax = MIN(100 - pplayer->economic.luxury
                                  - pplayer->economic.science, max);
      pplayer->economic.luxury = 100 - pplayer->economic.science
                                 - pplayer->economic.tax;

      notify_player(pplayer, NULL, E_REVOLT_DONE, ftc_server,
                    _("The tax rates for the %s are changed from "
                      "%3d%%/%3d%%/%3d%% (tax/luxury/science) to "
                      "%3d%%/%3d%%/%3d%%."), 
                    nation_plural_for_player(pplayer),
                    save_tax, save_luxury, save_science,
                    pplayer->economic.tax, pplayer->economic.luxury,
                    pplayer->economic.science);
    }
  }

  check_player_max_rates(pplayer);
  city_refresh_for_player(pplayer);
  player_research_update(pplayer);
  send_player_info_c(pplayer, pplayer->connections);
}

/**************************************************************************
  Called by the client or AI to change government.
**************************************************************************/
void handle_player_change_government(struct player *pplayer, int government)
{
  int turns;
  struct government *gov = government_by_number(government);

  if (!gov || !can_change_to_government(pplayer, gov)) {
    return;
  }

  log_debug("Government changed for %s. Target government is %s; "
            "old %s. Revofin %d, Turn %d.", player_name(pplayer),
            government_rule_name(gov),
            government_rule_name(government_of_player(pplayer)),
            pplayer->revolution_finishes, game.info.turn);

  /* Set revolution_finishes value. */
  if (pplayer->revolution_finishes > 0) {
    /* Player already has an active revolution.  Note that the finish time
     * may be in the future (we're waiting for it to finish), the current
     * turn (it just finished - but isn't reset until the end of the turn)
     * or even in the past (if the player is in anarchy and hasn't chosen
     * a government). */
    turns = pplayer->revolution_finishes - game.info.turn;
  } else if ((pplayer->ai_controlled && !ai_handicap(pplayer, H_REVOLUTION))
	     || get_player_bonus(pplayer, EFT_NO_ANARCHY)) {
    /* AI players without the H_REVOLUTION handicap can skip anarchy */
    turns = 0;
  } else if (game.server.revolution_length == 0) {
    turns = fc_rand(5) + 1;
  } else {
    turns = game.server.revolution_length;
  }

  pplayer->government = game.government_during_revolution;
  pplayer->target_government = gov;
  pplayer->revolution_finishes = game.info.turn + turns;

  log_debug("Revolution started for %s. Target government is %s. "
            "Revofin %d (%d).", player_name(pplayer),
            government_rule_name(pplayer->target_government),
            pplayer->revolution_finishes, game.info.turn);

  /* Now see if the revolution is instantaneous. */
  if (turns <= 0
      && pplayer->target_government != game.government_during_revolution) {
    finish_revolution(pplayer);
    return;
  } else if (turns > 0) {
    notify_player(pplayer, NULL, E_REVOLT_START, ftc_server,
                  /* TRANS: this is a message event so don't make it
                   * too long. */
                  PL_("The %s have incited a revolt! "
                      "%d turn of anarchy will ensue! "
                      "Target government is %s.",
                      "The %s have incited a revolt! "
                      "%d turns of anarchy will ensue! "
                      "Target government is %s.",
                      turns),
                  nation_plural_for_player(pplayer),
                  turns,
                  government_name_translation(pplayer->target_government));
  } else {
    fc_assert(pplayer->target_government == game.government_during_revolution);
    notify_player(pplayer, NULL, E_REVOLT_START, ftc_server,
                  _("Revolution: returning to anarchy."));
  }

  check_player_max_rates(pplayer);
  city_refresh_for_player(pplayer);
  send_player_info_c(pplayer, pplayer->connections);

  log_debug("Government change complete for %s. Target government is %s; "
            "now %s. Turn %d; revofin %d.", player_name(pplayer),
            government_rule_name(pplayer->target_government),
            government_rule_name(government_of_player(pplayer)),
            game.info.turn, pplayer->revolution_finishes);
}

/**************************************************************************
  See if the player has finished their revolution.  This function should
  be called at the beginning of a player's phase.
**************************************************************************/
void update_revolution(struct player *pplayer)
{
  /* The player's revolution counter is stored in the revolution_finishes
   * field.  This value has the following meanings:
   *   - If negative (-1), then the player is not in a revolution.  In this
   *     case the player should never be in anarchy.
   *   - If positive, the player is in the middle of a revolution.  In this
   *     case the value indicates the turn in which the revolution finishes.
   *     * If this value is > than the current turn, then the revolution is
   *       in progress.  In this case the player should always be in anarchy.
   *     * If the value is == to the current turn, then the revolution is
   *       finished.  The player may now choose a government.  However the
   *       value isn't reset until the end of the turn.  If the player has
   *       chosen a government by the end of the turn, then the revolution is
   *       over and the value is reset to -1.
   *     * If the player doesn't pick a government then the revolution
   *       continues.  At this point the value is <= to the current turn,
   *       and the player can leave the revolution at any time.  The value
   *       is reset at the end of any turn when a non-anarchy government is
   *       chosen.
   */
  log_debug("Update revolution for %s. Current government %s, "
            "target %s, revofin %d, turn %d.", player_name(pplayer),
            government_rule_name(government_of_player(pplayer)),
            pplayer->target_government
            ? government_rule_name(pplayer->target_government) : "(none)",
            pplayer->revolution_finishes, game.info.turn);
  if (government_of_player(pplayer) == game.government_during_revolution
      && pplayer->revolution_finishes <= game.info.turn) {
    if (pplayer->target_government != game.government_during_revolution) {
      /* If the revolution is over and a target government is set, go into
       * the new government. */
      log_debug("Update: finishing revolution for %s.", player_name(pplayer));
      finish_revolution(pplayer);
    } else {
      /* If the revolution is over but there's no target government set,
       * alert the player. */
      notify_player(pplayer, NULL, E_REVOLT_DONE, ftc_any,
                    _("You should choose a new government from the "
                      "government menu."));
    }
  } else if (government_of_player(pplayer) != game.government_during_revolution
	     && pplayer->revolution_finishes < game.info.turn) {
    /* Reset the revolution counter.  If the player has another revolution
     * they'll have to re-enter anarchy. */
    log_debug("Update: resetting revofin for %s.", player_name(pplayer));
    pplayer->revolution_finishes = -1;
    send_player_info_c(pplayer, pplayer->connections);
  }
}

/**************************************************************************
The following checks that government rates are acceptable for the present
form of government. Has to be called when switching governments or when
toggling from AI to human.
**************************************************************************/
void check_player_max_rates(struct player *pplayer)
{
  struct player_economic old_econ = pplayer->economic;

  pplayer->economic = player_limit_to_max_rates(pplayer);
  if (old_econ.tax > pplayer->economic.tax) {
    notify_player(pplayer, NULL, E_NEW_GOVERNMENT, ftc_server,
                  _("Tax rate exceeded the max rate; adjusted."));
  }
  if (old_econ.science > pplayer->economic.science) {
    notify_player(pplayer, NULL, E_NEW_GOVERNMENT, ftc_server,
                  _("Science rate exceeded the max rate; adjusted."));
  }
  if (old_econ.luxury > pplayer->economic.luxury) {
    notify_player(pplayer, NULL, E_NEW_GOVERNMENT, ftc_server,
                  _("Luxury rate exceeded the max rate; adjusted."));
  }
}

/**************************************************************************
  After the alliance is breaken, we need to do two things:
  - Inform clients that they cannot see units inside the former's ally
    cities
  - Remove units stacked together
**************************************************************************/
void update_players_after_alliance_breakup(struct player* pplayer,
                                          struct player* pplayer2)
{
  /* The client needs updated diplomatic state, because it is used
   * during calculation of new states of occupied flags in cities */
   send_player_all_c(pplayer, NULL);
   send_player_all_c(pplayer2, NULL);
   remove_allied_visibility(pplayer, pplayer2);
   remove_allied_visibility(pplayer2, pplayer);    
   resolve_unit_stacks(pplayer, pplayer2, TRUE);
}

/**************************************************************************
  If there's any units of new_owner on tile, they claim bases.
**************************************************************************/
static void maybe_claim_base(struct tile *ptile, struct player *new_owner,
                             struct player *old_owner)
{
  bool claim = FALSE;

  if (BORDERS_DISABLED == game.info.borders) {
    return;
  }

  unit_list_iterate(ptile->units, punit) {
    if (unit_owner(punit) == new_owner
        && tile_has_claimable_base(ptile, unit_type(punit))) {
      claim = TRUE;
      break;
    }
  } unit_list_iterate_end;

  if (claim) {
    map_claim_ownership(ptile, new_owner, ptile);

    /* Clear borders from old owner. New owner may not know all those
     * tiles and thus does not claim them when borders mode is less
     * than EXPAND. */
    map_clear_border(ptile);
    map_claim_border(ptile, new_owner);
    city_thaw_workers_queue();
    city_refresh_queue_processing();
  }
}

/**************************************************************************
  Two players enter war.
**************************************************************************/
void enter_war(struct player *pplayer, struct player *pplayer2)
{
  /* Claim bases where units are already standing */
  whole_map_iterate(ptile) {
    struct player *old_owner = tile_owner(ptile);

    if (old_owner == pplayer2) {
      maybe_claim_base(ptile, pplayer, old_owner);
    } else if (old_owner == pplayer) {
      maybe_claim_base(ptile, pplayer2, old_owner);
    }
  } whole_map_iterate_end;
}

/**************************************************************************
  Handles a player cancelling a "pact" with another player.

  packet.id is id of player we want to cancel a pact with
  packet.val1 is a special value indicating what kind of treaty we want
    to break. If this is CLAUSE_VISION we break shared vision. If it is
    a pact treaty type, we break one pact level. If it is CLAUSE_LAST
    we break _all_ treaties and go straight to war.
**************************************************************************/
void handle_diplomacy_cancel_pact(struct player *pplayer,
				  int other_player_id,
				  enum clause_type clause)
{
  enum diplstate_type old_type;
  enum diplstate_type new_type;
  enum dipl_reason diplcheck;
  bool repeat = FALSE;
  struct player *pplayer2 = player_by_number(other_player_id);
  struct player_diplstate *ds_plrplr2, *ds_plr2plr;

  if (NULL == pplayer2) {
    return;
  }

  old_type = player_diplstate_get(pplayer, pplayer2)->type;

  if (clause == CLAUSE_VISION) {
    if (!gives_shared_vision(pplayer, pplayer2)) {
      return;
    }
    remove_shared_vision(pplayer, pplayer2);
    notify_player(pplayer2, NULL, E_TREATY_BROKEN, ftc_server,
                  _("%s no longer gives us shared vision!"),
                  player_name(pplayer));
    return;
  }

  diplcheck = pplayer_can_cancel_treaty(pplayer, pplayer2);

  /* The senate may not allow you to break the treaty.  In this case you
   * must first dissolve the senate then you can break it. */
  if (diplcheck == DIPL_SENATE_BLOCKING) {
    notify_player(pplayer, NULL, E_TREATY_BROKEN, ftc_server,
                  _("The senate will not allow you to break treaty "
                    "with the %s.  You must either dissolve the senate "
                    "or wait until a more timely moment."),
                  nation_plural_for_player(pplayer2));
    return;
  }

  if (diplcheck != DIPL_OK) {
    return;
  }

  reject_all_treaties(pplayer);
  reject_all_treaties(pplayer2);
  /* else, breaking a treaty */

  /* check what the new status will be */
  switch(old_type) {
  case DS_NO_CONTACT: /* possible if someone declares war on our ally */
  case DS_ARMISTICE:
  case DS_CEASEFIRE:
  case DS_PEACE:
    new_type = DS_WAR;
    break;
  case DS_ALLIANCE:
    new_type = DS_ARMISTICE;
    break;
  default:
    log_error("non-pact diplstate in handle_player_cancel_pact");
    return;
  }

  ds_plrplr2 = player_diplstate_get(pplayer, pplayer2);
  ds_plr2plr = player_diplstate_get(pplayer2, pplayer);

  /* do the change */
  ds_plrplr2->type = ds_plr2plr->type = new_type;
  ds_plrplr2->turns_left = ds_plr2plr->turns_left = 16;

  /* If the old state was alliance, the players' units can share tiles
     illegally, and we need to call resolve_unit_stacks() */
  if (old_type == DS_ALLIANCE) {
    update_players_after_alliance_breakup(pplayer, pplayer2);
  }

  /* if there's a reason to cancel the pact, do it without penalty */
  /* FIXME: in the current implementation if you break more than one
   * treaty simultaneously it may partially succed: the first treaty-breaking
   * will happen but the second one will fail. */
  if (get_player_bonus(pplayer, EFT_HAS_SENATE) > 0 && !repeat) {
    if (ds_plrplr2->has_reason_to_cancel > 0) {
      notify_player(pplayer, NULL, E_TREATY_BROKEN, ftc_server,
                    _("The senate passes your bill because of the "
                      "constant provocations of the %s."),
                    nation_plural_for_player(pplayer2));
    } else if (new_type == DS_WAR) {
      notify_player(pplayer, NULL, E_TREATY_BROKEN, ftc_server,
                    _("The senate refuses to break treaty with the %s, "
                      "but you have no trouble finding a new senate."),
                    nation_plural_for_player(pplayer2));
    }
  }
  if (new_type == DS_WAR) {
    call_incident(INCIDENT_WAR, pplayer, pplayer2);

    enter_war(pplayer, pplayer2);
  }
  ds_plrplr2->has_reason_to_cancel = 0;

  send_player_all_c(pplayer, NULL);
  send_player_all_c(pplayer2, NULL);

  if (old_type == DS_ALLIANCE) {
    /* Inform clients about units that have been hidden.  Units in cities
     * and transporters are visible to allies but not visible once the
     * alliance is broken.  We have to call this after resolve_unit_stacks
     * because that function may change units' locations.  It also sends
     * out new city info packets to tell the client about occupied cities,
     * so it should also come after the send_player_all_c calls above. */
    remove_allied_visibility(pplayer, pplayer2);
    remove_allied_visibility(pplayer2, pplayer);
  }

  /* 
   * Refresh all cities which have a unit of the other side within
   * city range. 
   */
  city_map_update_all_cities_for_player(pplayer);
  city_map_update_all_cities_for_player(pplayer2);
  sync_cities();

  notify_player(pplayer, NULL, E_TREATY_BROKEN, ftc_server,
                _("The diplomatic state between the %s "
                  "and the %s is now %s."),
                nation_plural_for_player(pplayer),
                nation_plural_for_player(pplayer2),
                diplstate_text(new_type));
  notify_player(pplayer2, NULL, E_TREATY_BROKEN, ftc_server,
                _(" %s canceled the diplomatic agreement! "
                  "The diplomatic state between the %s and the %s "
                  "is now %s."),
                player_name(pplayer),
                nation_plural_for_player(pplayer2),
                nation_plural_for_player(pplayer),
                diplstate_text(new_type));

  /* Check fall-out of a war declaration. */
  players_iterate_alive(other) {
    if (other != pplayer && other != pplayer2
        && new_type == DS_WAR && pplayers_allied(pplayer2, other)
        && pplayers_allied(pplayer, other)) {
      if (!players_on_same_team(pplayer, other)) {
        /* If an ally declares war on another ally, break off your alliance
         * to the aggressor. This prevents in-alliance wars, which are not
         * permitted. */
        notify_player(other, NULL, E_TREATY_BROKEN, ftc_server,
                      _("%s has attacked your ally %s! "
                        "You cancel your alliance to the aggressor."),
                      player_name(pplayer),
                      player_name(pplayer2));
        player_diplstate_get(other, pplayer)->has_reason_to_cancel = 1;
        handle_diplomacy_cancel_pact(other, player_number(pplayer),
                                     CLAUSE_ALLIANCE);
      } else {
        /* We are in the same team as the agressor; we cannot break 
         * alliance with him. We trust our team mate and break alliance
         * with the attacked player */
        notify_player(other, NULL, E_TREATY_BROKEN, ftc_server,
                      _("Your team mate %s declared war on %s. "
                        "You are obligated to cancel alliance with %s."),
                      player_name(pplayer),
                      nation_plural_for_player(pplayer2),
                      player_name(pplayer2));
        handle_diplomacy_cancel_pact(other, player_number(pplayer2), CLAUSE_ALLIANCE);
      }
    }
  } players_iterate_alive_end;
}

/**************************************************************************
  Send information about removed (unused) players.
**************************************************************************/
static void send_player_remove_info_c(const struct player_slot *pslot,
                                      struct conn_list *dest)
{
  if (!dest) {
    dest = game.est_connections;
  }

  fc_assert_ret(!player_slot_is_used(pslot));

  conn_list_iterate(dest, pconn) {
    dsend_packet_player_remove(pconn, player_slot_index(pslot));
  } conn_list_iterate_end;
}

/**************************************************************************
  Send all information about a player (player_info and all
  player_diplstates) to the given connections.

  Send all players if src is NULL; send to all connections if dest is NULL.

  This function also sends the diplstate of the player. So take care, that
  all players are defined in the client and in the server. To create a
  player without sending the diplstate, use send_player_info_c().
**************************************************************************/
void send_player_all_c(struct player *src, struct conn_list *dest)
{
  send_player_info_c(src, dest);
  send_player_diplstate_c(src, dest);
}

/**************************************************************************
  Send information about player slot 'src', or all valid (i.e. used and
  initialized) players if 'src' is NULL, to specified clients 'dest'.
  If 'dest' is NULL, it is treated as game.est_connections.

  Note: package_player_info contains incomplete info if it has NULL as a
        dest arg and and info is < INFO_EMBASSY.
  NB: If 'src' is NULL (meaning send information about all players) this
  function will only send info for used players, i.e. player slots with
  a player defined.
**************************************************************************/
void send_player_info_c(struct player *src, struct conn_list *dest)
{
  if (src != NULL) {
    send_player_info_c_real(src, dest);
    return;
  }

  players_iterate(pplayer) {
    send_player_info_c_real(pplayer, dest);
  } players_iterate_end;
}

/**************************************************************************
  Really send information. If 'dest' is NULL, then it is set to
  game.est_connections.
**************************************************************************/
static void send_player_info_c_real(struct player *src,
                                    struct conn_list *dest)
{
  struct packet_player_info info;

  fc_assert_ret(src != NULL);

  if (!dest) {
    dest = game.est_connections;
  }

  package_player_common(src, &info);

  conn_list_iterate(dest, pconn) {
    if (NULL == pconn->playing && pconn->observer) {
      /* Global observer. */
      package_player_info(src, &info, pconn->playing, INFO_FULL);
    } else if (NULL != pconn->playing) {
      /* Players (including regular observers) */
      package_player_info(src, &info, pconn->playing, INFO_MINIMUM);
    } else {
      package_player_info(src, &info, NULL, INFO_MINIMUM);
    }
    send_packet_player_info(pconn, &info);
  } conn_list_iterate_end;
}

/**************************************************************************
  Identical to send_player_info_c(), but sends the diplstate of the
  player.

  This function solves one problem of using an extra packet for the
  diplstate. It can only be send if the player exists at the destination.
  Thus, this function should be called after the player(s) exists on both
  sides of the connection.
**************************************************************************/
void send_player_diplstate_c(struct player *src, struct conn_list *dest)
{
  if (src != NULL) {
    send_player_diplstate_c_real(src, dest);
    return;
  }

  players_iterate(pplayer) {
    send_player_diplstate_c_real(pplayer, dest);
  } players_iterate_end;
}

/**************************************************************************
  Really send information. If 'dest' is NULL, then it is set to
  game.est_connections.
**************************************************************************/
static void send_player_diplstate_c_real(struct player *plr1,
                                         struct conn_list *dest)
{
  fc_assert_ret(plr1 != NULL);

  if (!dest) {
    dest = game.est_connections;
  }

  conn_list_iterate(dest, pconn) {
    players_iterate(plr2) {
      struct packet_player_diplstate packet_ds;

      if (NULL == pconn->playing && pconn->observer) {
        /* Global observer. */
        package_player_diplstate(plr1, plr2, &packet_ds, pconn->playing,
                                 INFO_FULL);
      } else if (NULL != pconn->playing) {
        /* Players (including regular observers) */
        package_player_diplstate(plr1, plr2, &packet_ds, pconn->playing,
                                 INFO_MINIMUM);
      } else {
        package_player_diplstate(plr1, plr2, &packet_ds, NULL,
                                 INFO_MINIMUM);
      }
      send_packet_player_diplstate(pconn, &packet_ds);
    } players_iterate_end;
  } conn_list_iterate_end;
}

/**************************************************************************
 Package player information that is always sent.
**************************************************************************/
static void package_player_common(struct player *plr,
                                  struct packet_player_info *packet)
{
  int i;

  packet->playerno = player_number(plr);
  sz_strlcpy(packet->name, player_name(plr));
  sz_strlcpy(packet->username, plr->username);
  packet->nation = plr->nation ? nation_number(plr->nation) : -1;
  packet->is_male=plr->is_male;
  packet->team = plr->team ? team_number(plr->team) : -1;
  packet->is_ready = plr->is_ready;
  packet->was_created = plr->was_created;
  if (city_styles != NULL) {
    packet->city_style = city_style_of_player(plr);
  } else {
    packet->city_style = 0;
  }

  packet->is_alive=plr->is_alive;
  packet->is_connected=plr->is_connected;
  packet->ai = plr->ai_controlled;
  packet->ai_skill_level = plr->ai_controlled
                           ? plr->ai_common.skill_level : 0;
  for (i = 0; i < player_slot_count(); i++) {
    packet->love[i] = plr->ai_common.love[i];
  }
  packet->barbarian_type = plr->ai_common.barbarian_type;

  packet->phase_done = plr->phase_done;
  packet->nturns_idle=plr->nturns_idle;

  for (i = 0; i < B_LAST/*improvement_count()*/; i++) {
    packet->wonders[i] = plr->wonders[i];
  }
  packet->science_cost = plr->ai_common.science_cost;
}

/**************************************************************************
  Package player info depending on info_level. We send everything to
  plr's connections, we send almost everything to players with embassy
  to plr, we send a little to players we are in contact with and almost
  nothing to everyone else.

  Receiver may be NULL in which cases dummy values are sent for some
  fields.
**************************************************************************/
static void package_player_info(struct player *plr,
                                struct packet_player_info *packet,
                                struct player *receiver,
                                enum plr_info_level min_info_level)
{
  enum plr_info_level info_level;
  enum plr_info_level highest_team_level;
  struct player_research* research = player_research_get(plr);
  struct government *pgov = NULL;

  if (receiver) {
    info_level = player_info_level(plr, receiver);
    info_level = MAX(min_info_level, info_level);
  } else {
    info_level = min_info_level;
  }

  /* We need to send all tech info for all players on the same
   * team, even if they are not in contact yet; otherwise we will
   * overwrite team research or confuse the client. */
  highest_team_level = info_level;
  players_iterate(aplayer) {
    if (players_on_same_team(plr, aplayer) && receiver) {
      highest_team_level = MAX(highest_team_level,
                               player_info_level(aplayer, receiver));
    }
  } players_iterate_end;

  if (plr->rgb != NULL) {
    packet->color_red = plr->rgb->r;
    packet->color_green = plr->rgb->g;
    packet->color_blue = plr->rgb->b;
  } else {
    /* In pregame, send the color we expect to use, for consistency with
     * '/list colors' etc. */
    const struct rgbcolor *preferred = player_preferred_color(plr);
    if (preferred != NULL) {
      packet->color_red = preferred->r;
      packet->color_green = preferred->g;
      packet->color_blue = preferred->b;
    } else {
      /* Can't tell the client 'no color', so use dummy values (black). */
      packet->color_red = 0;
      packet->color_green = 0;
      packet->color_blue = 0;
    }
  }

  /* Only send score if we have contact */
  if (info_level >= INFO_MEETING) {
    packet->score = plr->score.game;
  } else {
    packet->score = 0;
  }

  if (info_level >= INFO_MEETING) {
    packet->gold = plr->economic.gold;
    pgov = government_of_player(plr);
  } else {
    packet->gold = 0;
    pgov = game.government_during_revolution;
  }
  packet->government = pgov ? government_number(pgov) : -1;
   
  /* Send diplomatic status of the player to everyone they are in
   * contact with. */
  if (info_level >= INFO_EMBASSY
      || (receiver
          && player_diplstate_get(receiver, plr)->contact_turns_left > 0)) {
    packet->target_government = plr->target_government
                                ? government_number(plr->target_government)
                                : -1;
    memset(&packet->real_embassy, 0, sizeof(packet->real_embassy));
    players_iterate(pother) {
      packet->real_embassy[player_index(pother)] =
        player_has_real_embassy(plr, pother);
    } players_iterate_end;
    packet->gives_shared_vision = plr->gives_shared_vision;
  } else {
    packet->target_government = packet->government;
    memset(&packet->real_embassy, 0, sizeof(packet->real_embassy));
    if (receiver && player_has_real_embassy(plr, receiver)) {
      packet->real_embassy[player_index(receiver)] = TRUE;
    }

    BV_CLR_ALL(packet->gives_shared_vision);
    if (receiver && gives_shared_vision(plr, receiver)) {
      BV_SET(packet->gives_shared_vision, player_index(receiver));
    }
  }

  /* Make absolutely sure - in case you lose your embassy! */
  if (info_level >= INFO_EMBASSY 
      || (receiver
	  && player_diplstate_get(plr, receiver)->type == DS_TEAM)) {
    packet->bulbs_last_turn = plr->bulbs_last_turn;
  } else {
    packet->bulbs_last_turn = 0;
  }

  /* Send most civ info about the player only to players who have an
   * embassy. */
  if (highest_team_level >= INFO_EMBASSY) {
    advance_index_iterate(A_FIRST, i) {
      packet->inventions[i] = 
        research->inventions[i].state + '0';
    } advance_index_iterate_end;
    packet->tax             = plr->economic.tax;
    packet->science         = plr->economic.science;
    packet->luxury          = plr->economic.luxury;
    packet->bulbs_researched = research->bulbs_researched;
    packet->techs_researched = research->techs_researched;
    packet->researching = research->researching;
    packet->future_tech = research->future_tech;
    packet->revolution_finishes = plr->revolution_finishes;
  } else {
    advance_index_iterate(A_FIRST, i) {
      packet->inventions[i] = '0';
    } advance_index_iterate_end;
    packet->tax             = 0;
    packet->science         = 0;
    packet->luxury          = 0;
    packet->bulbs_researched= 0;
    packet->techs_researched= 0;
    packet->researching     = A_UNKNOWN;
    packet->future_tech     = 0;
    packet->revolution_finishes = -1;
  }

  /* We have to inform the client that the other players also know
   * A_NONE. */
  packet->inventions[A_NONE] = research->inventions[A_NONE].state + '0';
  packet->inventions[advance_count()] = '\0';
#ifdef DEBUG
  log_verbose("Player%d inventions:%s",
              player_number(plr), packet->inventions);
#endif

  if (info_level >= INFO_FULL
      || (receiver
          && player_diplstate_get(plr, receiver)->type == DS_TEAM)) {
    packet->tech_goal       = research->tech_goal;
  } else {
    packet->tech_goal       = A_UNSET;
  }

  /* 
   * This may be an odd time to check these values but we can be sure
   * to have a consistent state here.
   */
  fc_assert(S_S_RUNNING != server_state()
            || A_UNSET == research->researching
            || is_future_tech(research->researching)
            || (A_NONE != research->researching
                && valid_advance_by_number(research->researching)));
  fc_assert(A_UNSET == research->tech_goal
            || (A_NONE != research->tech_goal
                && valid_advance_by_number(research->tech_goal)));
}

/**************************************************************************
  Package player diplstate depending on info_level. We send everything to
  plr's connections, we send almost everything to players with embassy
  to plr, we send a little to players we are in contact with and almost
  nothing to everyone else.

  Receiver may be NULL in which cases dummy values are sent for some
  fields.
**************************************************************************/
static void package_player_diplstate(struct player *plr1,
                                     struct player *plr2,
                                     struct packet_player_diplstate *packet_ds,
                                     struct player *receiver,
                                     enum plr_info_level min_info_level)
{
  enum plr_info_level info_level;
  struct player_diplstate *ds = player_diplstate_get(plr1, plr2);

  if (receiver) {
    info_level = player_info_level(plr1, receiver);
    info_level = MAX(min_info_level, info_level);
  } else {
    info_level = min_info_level;
  }

  packet_ds->plr1 = player_index(plr1);
  packet_ds->plr2 = player_index(plr2);
  /* A unique id for each combination is calculated here. */
  packet_ds->diplstate_id = packet_ds->plr1 * MAX_NUM_PLAYER_SLOTS
                            + packet_ds->plr2;

  /* Send diplomatic status of the player to everyone they are in
   * contact with (embassy, remaining contact turns, the receiver). */
  if (info_level >= INFO_EMBASSY
      || (receiver
          && player_diplstate_get(receiver, plr1)->contact_turns_left > 0)
      || (receiver && receiver == plr2)) {
    packet_ds->type                 = ds->type;
    packet_ds->turns_left           = ds->turns_left;
    packet_ds->has_reason_to_cancel = ds->has_reason_to_cancel;
    packet_ds->contact_turns_left   = ds->contact_turns_left;
  } else {
    packet_ds->type                 = DS_WAR;
    packet_ds->turns_left           = 0;
    packet_ds->has_reason_to_cancel = 0;
    packet_ds->contact_turns_left   = 0;
  }
}

/**************************************************************************
  Return level of information player should receive about another.
**************************************************************************/
static enum plr_info_level player_info_level(struct player *plr,
					     struct player *receiver)
{
  if (S_S_RUNNING > server_state()) {
    return INFO_MINIMUM;
  }
  if (plr == receiver) {
    return INFO_FULL;
  }
  if (receiver && player_has_embassy(receiver, plr)) {
    return INFO_EMBASSY;
  }
  if (receiver && could_intel_with_player(receiver, plr)) {
    return INFO_MEETING;
  }
  return INFO_MINIMUM;
}

/**************************************************************************
  Convenience function to return "reply" destination connection list
  for player: pplayer->current_conn if set, else pplayer->connections.
**************************************************************************/
struct conn_list *player_reply_dest(struct player *pplayer)
{
  return (pplayer->current_conn ?
	  pplayer->current_conn->self :
	  pplayer->connections);
}

/**************************************************************************
  Call first_contact function if such is defined for player
**************************************************************************/
static void call_first_contact(struct player *pplayer, struct player *aplayer)
{
  CALL_PLR_AI_FUNC(first_contact, pplayer, pplayer, aplayer);
}

/****************************************************************************
  Initialize ANY newly-created player on the server.

  The initmap option is used because we don't want to initialize the map
  before the x and y sizes have been determined.  This should generally
  be FALSE in pregame.

  The needs_team options should be set for players who should be assigned
  a team.  They will be put on their own newly-created team.
****************************************************************************/
void server_player_init(struct player *pplayer, bool initmap,
                        bool needs_team)
{
  player_status_reset(pplayer);

  pplayer->server.capital = FALSE;
  BV_CLR_ALL(pplayer->server.really_gives_vision);
  BV_CLR_ALL(pplayer->server.debug);

  player_map_free(pplayer);
  pplayer->server.private_map = NULL;

  if (initmap) {
    player_map_init(pplayer);
  }
  if (needs_team) {
    team_add_player(pplayer, NULL);
  }

  /* This must be done after team information is initialised
   * as it might be needed to determine max rate effects. 
   * Sometimes this server_player_init() gets called twice
   * with only latter one having needs_team set. We don't
   * want to call player_limit_to_max_rates() at first time
   * when team is not yet set. It's callers responsibility
   * to always have one server_player_init() call with
   * needs_team TRUE. */
  if (needs_team) {
    pplayer->economic = player_limit_to_max_rates(pplayer);
  }

  adv_data_default(pplayer);

  /* We don't push this in calc_civ_score(), or it will be reset
   * every turn. */
  pplayer->score.units_built = 0;
  pplayer->score.units_killed = 0;
  pplayer->score.units_lost = 0;

  /* No delegation. */
  pplayer->server.delegate_to[0] = '\0';
  pplayer->server.orig_username[0] = '\0';
}

/****************************************************************************
  If a player's color will be predictable when colors are assigned (or
  assignment has already happened), return that color. Otherwise (if the
  player's color is yet to be assigned randomly), return NULL.
****************************************************************************/
const struct rgbcolor *player_preferred_color(struct player *pplayer)
{
  if (pplayer->rgb) {
    return pplayer->rgb;
  } else if (playercolor_count() == 0) {
    /* If a ruleset isn't loaded, there are no colors to choose from. */
    return NULL;
  } else {
    int colorid;
    switch (game.server.plrcolormode) {
    case PLRCOL_PLR_SET: /* player color (set) */
    case PLRCOL_PLR_RANDOM: /* player color (random) */
      /* These depend on other players and will be assigned at game start. */
      return NULL;
    default:
      log_error("Invalid value for 'game.server.plrcolormode' (%d)!",
                game.server.plrcolormode);
      /* no break - using 'PLRCOL_PLR_ORDER' as fallback */
    case PLRCOL_PLR_ORDER: /* player color (ordered) */
      colorid = player_number(pplayer) % playercolor_count();
      break;
    case PLRCOL_TEAM_ORDER: /* team color (ordered) */
      colorid = team_number(pplayer->team) % playercolor_count();
      break;
    }
    return playercolor_get(colorid);
  }
}

/****************************************************************************
  Permanently assign colors to any players that don't already have them.
  First assign preferred colors, then assign the rest randomly, trying to
  avoid clashes.
****************************************************************************/
void assign_player_colors(void)
{
  struct rgbcolor_list *spare_colors =
    rgbcolor_list_copy(game.server.plr_colors);
  int needed = player_count();

  players_iterate(pplayer) {
    const struct rgbcolor *autocolor;
    /* Assign the deterministic colors. */
    if (!pplayer->rgb
        && (autocolor = player_preferred_color(pplayer))) {
      player_set_color(pplayer, autocolor);
    }
    if (pplayer->rgb) {
      /* One fewer random color needed. */
      needed--;
      /* Try to avoid clashes between explicit and random colors. */
      rgbcolor_list_iterate(spare_colors, prgbcolor) {
        if (rgbcolors_are_equal(pplayer->rgb, prgbcolor)) {
          rgbcolor_list_remove(spare_colors, prgbcolor);
        }
      } rgbcolor_list_iterate_end;
    }
  } players_iterate_end;

  if (needed == 0) {
    /* No random colors needed */
    rgbcolor_list_destroy(spare_colors);
    return;
  }

  fc_assert(game.server.plrcolormode == PLRCOL_PLR_RANDOM
            || game.server.plrcolormode == PLRCOL_PLR_SET);

  if (needed > rgbcolor_list_size(spare_colors)) {
    log_verbose("Not enough unique colors for all players; there will be "
                "duplicates");
    /* Fallback: start again from full set of ruleset colors.
     * No longer attempt to avoid clashes with explicitly assigned colors. */
    rgbcolor_list_destroy(spare_colors);
    spare_colors = rgbcolor_list_copy(game.server.plr_colors);
  }
  /* We may still not have enough, if there are more players than
   * ruleset-defined colors. If so, top up with duplicates. */
  if (needed > rgbcolor_list_size(spare_colors)) {
    int i, origsize = rgbcolor_list_size(spare_colors);
    /* Shuffle so that duplicates aren't biased to start of list */
    rgbcolor_list_shuffle(spare_colors);
    /* Duplication process avoids one color being hit lots of times */
    for (i = origsize; i < needed; i++) {
      rgbcolor_list_append(spare_colors,
                           rgbcolor_list_get(spare_colors, i - origsize));
    }
  }
  /* Shuffle (including mixing any duplicates up) */
  rgbcolor_list_shuffle(spare_colors);

  /* Finally, assign shuffled colors to players. */
  players_iterate(pplayer) {
    if (!pplayer->rgb) {
      player_set_color(pplayer, rgbcolor_list_front(spare_colors));
      rgbcolor_list_pop_front(spare_colors);
    }
  } players_iterate_end;

  rgbcolor_list_destroy(spare_colors);
}

/****************************************************************************
  Set the player's color. If 'prgbcolor' is not NULL the caller should free
  the pointer, as player_set_color() copies the data.
****************************************************************************/
void server_player_set_color(struct player *pplayer,
                             const struct rgbcolor *prgbcolor)
{
  if (prgbcolor != NULL) {
    player_set_color(pplayer, prgbcolor);
  } else {
    /* Server only: this can be NULL in pregame. */
    fc_assert_ret(!game_was_started());
    rgbcolor_destroy(pplayer->rgb);
    pplayer->rgb = NULL;
  }
  /* Update clients */
  send_player_info_c(pplayer, NULL);
}

/****************************************************************************
  Return the player color as featured text string.
  (In pregame, this uses the color the player will take, if known, even if
  not assigned yet.)
****************************************************************************/
const char *player_color_ftstr(struct player *pplayer)
{
  static char buf[64];
  char hex[16];
  const struct rgbcolor *prgbcolor;

  fc_assert_ret_val(pplayer != NULL, NULL);

  buf[0] = '\0';
  prgbcolor = player_preferred_color(pplayer);
  if (prgbcolor != NULL
      && rgbcolor_to_hex(prgbcolor, hex, sizeof(hex))) {
    struct ft_color plrcolor = FT_COLOR("#000000", hex);

    featured_text_apply_tag(hex, buf, sizeof(buf), TTT_COLOR, 0,
                            FT_OFFSET_UNSET, plrcolor);
  } else {
    cat_snprintf(buf, sizeof(buf), _("no color"));
  }

  return buf;
}

/********************************************************************** 
  Creates a new, uninitialized, used player slot. You should probably
  call server_player_init() to initialize it, and send_player_info_c()
  later to tell clients about it.

  May return NULL if creation was not possible.
***********************************************************************/
struct player *server_create_player(int player_id, const char *ai_type,
                                    struct rgbcolor *prgbcolor)
{
  struct player_slot *pslot;
  struct player *pplayer;

  pslot = player_slot_by_number(player_id);
  fc_assert(NULL == pslot || !player_slot_is_used(pslot));

  pplayer = player_new(pslot);
  if (NULL == pplayer) {
    return NULL;
  }

  pplayer->ai = ai_type_by_name(ai_type);

  if (pplayer->ai == NULL) {
    player_destroy(pplayer);
    return NULL;
  }

  adv_data_init(pplayer);

  CALL_FUNC_EACH_AI(player_alloc, pplayer);

  /* TODO: Do we really need this server_player_init() here? All our callers
   *       will later make another server_player_init() call anyway, with boolean
   *       parameters set to what they really need. */
  server_player_init(pplayer, FALSE, FALSE);

  if (prgbcolor) {
    player_set_color(pplayer, prgbcolor);
  } else if (game_was_started()) {
    /* Find a color for the new player. */
    assign_player_colors();
  }

  return pplayer;
}

/********************************************************************** 
  This function does _not_ close any connections attached to this
  player. The function cut_connection() is used for that. Be sure
  to send_player_slot_info_c() afterwards to tell clients that the
  player slot has become unused.
***********************************************************************/
void server_remove_player(struct player *pplayer)
{
  const struct player_slot *pslot;

  fc_assert_ret(NULL != pplayer);

  /* save player slot */
  pslot = pplayer->slot;

  log_normal(_("Removing player %s."), player_name(pplayer));

  notify_conn(pplayer->connections, NULL, E_CONNECTION, ftc_server,
              _("You've been removed from the game!"));

  notify_conn(game.est_connections, NULL, E_CONNECTION, ftc_server,
              _("%s has been removed from the game."),
              player_name(pplayer));

  if (is_barbarian(pplayer)) {
    server.nbarbarians--;
  }

  /* Don't use conn_list_iterate here because connection_detach() can be
   * recursive and free the next connection pointer. */
  while (conn_list_size(pplayer->connections) > 0) {
    connection_detach(conn_list_get(pplayer->connections, 0));
  }

  script_server_remove_exported_object(pplayer);
  /* Clear data saved in the other player structs. */
  players_iterate(aplayer) {
    BV_CLR(aplayer->real_embassy, player_index(pplayer));
    if (gives_shared_vision(aplayer, pplayer)) {
      remove_shared_vision(aplayer, pplayer);
    }
  } players_iterate_end;

  /* Remove citizens of this player from the cities of all other players. */
  /* FIXME: add a special case if the server quits - no need to run this for
   *        each player in that case. */
  if (game.info.citizen_nationality) {
    cities_iterate(pcity) {
      if (city_owner(pcity) != pplayer) {
        citizens nationality = citizens_nation_get(pcity, pplayer->slot);
        if (nationality != 0) {
          /* Change nationality of the citizens to the nationality of the
           * city owner. */
          citizens_nation_move(pcity, pplayer->slot, city_owner(pcity)->slot,
                               nationality);
          city_refresh_queue_add(pcity);
        }
      }
    } cities_iterate_end

    city_refresh_queue_processing();
  }

  /* AI type lost control of this player */
  CALL_PLR_AI_FUNC(lost_control, pplayer, pplayer);

  /* We have to clear all player data before the ai memory is freed because
   * some function may depend on it. */
  player_clear(pplayer, TRUE);
  player_map_free(pplayer);

  /* Destroy advisor and ai data. */
  CALL_FUNC_EACH_AI(player_free, pplayer);

  adv_data_close(pplayer);
  player_destroy(pplayer);

  send_updated_vote_totals(NULL);
  /* must be called after the player was destroyed */
  send_player_remove_info_c(pslot, NULL);

  /* Recalculate borders. */
  map_calculate_borders();
}

/**************************************************************************
  The following limits a player's rates to those that are acceptable for the
  present form of government.  If a rate exceeds maxrate for this government,
  it adjusts rates automatically adding the extra to the 2nd highest rate,
  preferring science to taxes and taxes to luxuries.
  (It assumes that for any government maxrate>=50)

  Returns actual max rate used. This function should be called after team
  information are defined.
**************************************************************************/
struct player_economic player_limit_to_max_rates(struct player *pplayer)
{
  int maxrate, surplus;
  struct player_economic economic;

  /* ai players allowed to cheat */
  if (pplayer->ai_controlled) {
    return pplayer->economic;
  }

  economic = pplayer->economic;

  maxrate = get_player_maxrate(pplayer);

  surplus = 0;
  if (economic.luxury > maxrate) {
    surplus += economic.luxury - maxrate;
    economic.luxury = maxrate;
  }
  if (economic.tax > maxrate) {
    surplus += economic.tax - maxrate;
    economic.tax = maxrate;
  }
  if (economic.science > maxrate) {
    surplus += economic.science - maxrate;
    economic.science = maxrate;
  }

  fc_assert(surplus % 10 == 0);
  while (surplus > 0) {
    if (economic.science < maxrate) {
      economic.science += 10;
    } else if (economic.tax < maxrate) {
      economic.tax += 10;
    } else if (economic.luxury < maxrate) {
      economic.luxury += 10;
    } else {
      fc_assert_msg(FALSE, "Failed to distribute the surplus. "
                    "maxrate = %d.", maxrate);
    }
    surplus -= 10;
  }

  return economic;
}

/****************************************************************************
  Check if this name is allowed for the player. Fill out the error message
  (a translated string to be sent to the client) if not.
****************************************************************************/
static bool server_player_name_is_allowed(const struct connection *caller,
                                          const struct player *pplayer,
                                          const struct nation_type *pnation,
                                          const char *name, char *error_buf,
                                          size_t error_buf_len)
{
  /* An empty name is surely not allowed. */
  if (0 == strlen(name)) {
    fc_strlcpy(error_buf, _("Please choose a non-blank name."),
               error_buf_len);
    return FALSE;
  }

  /* Any name already taken is not allowed. */
  players_iterate(other_player) {
    if (other_player == pplayer) {
      /* We don't care if we're the one using the name/nation. */
      continue;
    } else if (NULL != pnation && other_player->nation == pnation) {
      /* FIXME: currently cannot use nation_of_player(other_player) as the
       * nation debug code is buggy and doesn't test nation for NULL. */
      fc_strlcpy(error_buf, _("That nation is already in use."),
                 error_buf_len);
      return FALSE;
    } else if (0 == fc_strcasecmp(player_name(other_player), name)) {
      fc_snprintf(error_buf, error_buf_len,
                  _("Another player already has the name '%s'. Please "
                    "choose another name."), name);
      return FALSE;
    }
  } players_iterate_end;

  if (NULL == pnation) {
    /* FIXME: currently cannot use nation_of_player(other_player) as the
     * nation debug code is buggy and doesn't test nation for NULL. */
    pnation = pplayer->nation;
  }

  /* Any name from the default list is always allowed. */
  if (NULL != pnation && NULL != nation_leader_by_name(pnation, name)) {
    return TRUE;
  }

  /* To prevent abuse, only players with HACK access (usually local
   * connections) can use non-ascii names. Otherwise players could use
   * confusing garbage names in multi-player games. */
  if (NULL != caller
      && caller->access_level < ALLOW_HACK
      && !is_ascii_name(name)) {
    fc_strlcpy(error_buf,
               _("Please choose a name containing only ASCII characters."),
               error_buf_len);
    return FALSE;
  }

  return TRUE;
}

/****************************************************************************
  Try to set the player name to 'name'. Else, find a default name. Returns
  TRUE on success.
****************************************************************************/
bool server_player_set_name_full(const struct connection *caller,
                                 struct player *pplayer,
                                 const struct nation_type *pnation,
                                 const char *name,
                                 char *error_buf, size_t error_buf_len)
{
  char real_name[MAX_LEN_NAME];
  char buf[256];
  int i;

  /* Always provide an error buffer. */
  if (NULL == error_buf) {
    error_buf = buf;
    error_buf_len = sizeof(buf);
  }
  error_buf[0] = '\0';

  if (NULL != name) {
    /* Ensure this is a correct name. */
    sz_strlcpy(real_name, name);
    remove_leading_trailing_spaces(real_name);
    real_name[0] = fc_toupper(real_name[0]);

    if (server_player_name_is_allowed(caller, pplayer, pnation, real_name,
                                      error_buf, error_buf_len)) {
      log_debug("Name of player nb %d set to \"%s\".",
                player_number(pplayer), real_name);
      fc_strlcpy(pplayer->name, real_name, sizeof(pplayer->name));
      return TRUE; /* Success! */
    } else {
      log_verbose("Failed to set the name of the player nb %d to \"%s\": %s",
                  player_number(pplayer), real_name, error_buf);
      /* Fallthrough. */
    }
  }

  if (NULL != caller) {
    /* If we want to test, let's fail here. */
    fc_assert(NULL != name);
    return FALSE;
  }

  if (NULL != name) {
    /* Try to append a number to 'real_name'. */
    char test[MAX_LEN_NAME];

    for (i = 2; i <= player_slot_count(); i++) {
      fc_snprintf(test, sizeof(test), "%s%d", real_name, i);
      if (server_player_name_is_allowed(caller, pplayer, pnation,
                                        test, error_buf, error_buf_len)) {
        log_verbose("Name of player nb %d set to \"%s\" instead.",
                    player_number(pplayer), test);
        fc_strlcpy(pplayer->name, test, sizeof(pplayer->name));
        return TRUE;
      } else {
        log_debug("Failed to set the name of the player nb %d to \"%s\": %s",
                  player_number(pplayer), test, error_buf);
      }
    }
  }

  /* Try a default name. */
  fc_snprintf(real_name, sizeof(real_name),
              _("Player no. %d"), player_number(pplayer));
  if (server_player_name_is_allowed(caller, pplayer, pnation,
                                    real_name, error_buf, error_buf_len)) {
    log_verbose("Name of player nb %d set to \"%s\".",
                player_number(pplayer), real_name);
    fc_strlcpy(pplayer->name, real_name, sizeof(pplayer->name));
    return TRUE;
  } else {
    log_debug("Failed to set the name of the player nb %d to \"%s\": %s",
              player_number(pplayer), real_name, error_buf);
  }

  /* Try a very default name... */
  for (i = 0; i < player_slot_count(); i++) {
    fc_snprintf(real_name, sizeof(real_name), _("Player no. %d"), i);
    if (server_player_name_is_allowed(caller, pplayer, pnation,
                                      real_name, error_buf, error_buf_len)) {
      log_verbose("Name of player nb %d to \"%s\".",
                  player_number(pplayer), real_name);
      fc_strlcpy(pplayer->name, real_name, sizeof(pplayer->name));
      return TRUE;
    } else {
      log_debug("Failed to set the name of the player nb %d to \"%s\": %s",
                player_number(pplayer), real_name, error_buf);
    }
  }

  /* This is really not normal... Maybe the size of 'real_name'
   * is not enough big, or a bug in server_player_name_is_allowed(). */
  fc_strlcpy(pplayer->name, _("A poorly-named player"),
             sizeof(pplayer->name));
  return FALSE; /* Let's say it's a failure. */
}

/****************************************************************************
  Try to set the player name to 'name'. Else, find a default name.
****************************************************************************/
void server_player_set_name(struct player *pplayer, const char *name)
{
  bool ret;

  ret = server_player_set_name_full(NULL, pplayer, NULL, name, NULL, 0);
  fc_assert(TRUE == ret);
}

/**************************************************************************
  Returns the default diplomatic state between 2 players.

  Mainly, this returns DS_WAR, but it can also return DS_PEACE if both
  players are allied with the same third player.
**************************************************************************/
static enum diplstate_type
get_default_diplstate(const struct player *pplayer1,
                      const struct player *pplayer2)
{
  players_iterate_alive(pplayer3) {
    if (pplayer3 != pplayer1
        && pplayer3 != pplayer2
        && pplayers_allied(pplayer3, pplayer1)
        && pplayers_allied(pplayer3, pplayer2)) {
      return DS_PEACE;
    }
  } players_iterate_alive_end;

  return DS_WAR;
}

/**************************************************************************
  Update contact info.
**************************************************************************/
void make_contact(struct player *pplayer1, struct player *pplayer2,
                  struct tile *ptile)
{
  struct player_diplstate *ds_plr1plr2, *ds_plr2plr1;

  if (pplayer1 == pplayer2
      || !pplayer1->is_alive
      || !pplayer2->is_alive) {
    return;
  }

  ds_plr1plr2 = player_diplstate_get(pplayer1, pplayer2);
  ds_plr2plr1 = player_diplstate_get(pplayer2, pplayer1);

  if (get_player_bonus(pplayer1, EFT_NO_DIPLOMACY) == 0
      && get_player_bonus(pplayer2, EFT_NO_DIPLOMACY) == 0) {
    ds_plr1plr2->contact_turns_left = game.server.contactturns;
    ds_plr2plr1->contact_turns_left = game.server.contactturns;
  }
  if (ds_plr1plr2->type == DS_NO_CONTACT) {
    enum diplstate_type new_state = get_default_diplstate(pplayer1,
                                                          pplayer2);

    ds_plr1plr2->type = new_state;
    ds_plr2plr1->type = new_state;
    ds_plr1plr2->first_contact_turn = game.info.turn;
    ds_plr2plr1->first_contact_turn = game.info.turn;
    notify_player(pplayer1, ptile, E_FIRST_CONTACT, ftc_server,
                  _("You have made contact with the %s, ruled by %s."),
                  nation_plural_for_player(pplayer2),
                  player_name(pplayer2));
    notify_player(pplayer2, ptile, E_FIRST_CONTACT, ftc_server,
                  _("You have made contact with the %s, ruled by %s."),
                  nation_plural_for_player(pplayer1),
                  player_name(pplayer1));
    send_player_all_c(pplayer1, pplayer2->connections);
    send_player_all_c(pplayer2, pplayer1->connections);
    send_player_all_c(pplayer1, pplayer1->connections);
    send_player_all_c(pplayer2, pplayer2->connections);
    if (pplayer1->ai_controlled) {
      call_first_contact(pplayer1, pplayer2);
    }
    if (pplayer2->ai_controlled && !pplayer1->ai_controlled) {
      call_first_contact(pplayer2, pplayer1);
    }
    return;
  } else {
    fc_assert(ds_plr2plr1->type != DS_NO_CONTACT);
  }
  if (player_has_embassy(pplayer1, pplayer2)
      || player_has_embassy(pplayer2, pplayer1)) {
    return; /* Avoid sending too much info over the network */
  }
  send_player_all_c(pplayer1, pplayer1->connections);
  send_player_all_c(pplayer2, pplayer2->connections);
}

/**************************************************************************
  Check if we make contact with anyone.
**************************************************************************/
void maybe_make_contact(struct tile *ptile, struct player *pplayer)
{
  square_iterate(ptile, 1, tile1) {
    struct city *pcity = tile_city(tile1);
    if (pcity) {
      make_contact(pplayer, city_owner(pcity), ptile);
    }
    unit_list_iterate_safe(tile1->units, punit) {
      make_contact(pplayer, unit_owner(punit), ptile);
    } unit_list_iterate_safe_end;
  } square_iterate_end;
}

/**************************************************************************
  Shuffle or reshuffle the player order, storing in static variables above.
**************************************************************************/
void shuffle_players(void)
{
  /* shuffled_order is defined global */
  int n = player_slot_count();
  int i;

  log_debug("shuffle_players: creating shuffled order");

  for (i = 0; i < n; i++) {
    shuffled_order[i] = i;
  }

  /* randomize it */
  array_shuffle(shuffled_order, n);

#ifdef DEBUG
  for (i = 0; i < n; i++) {
    log_debug("shuffled_order[%d] = %d", i, shuffled_order[i]);
  }
#endif
}

/**************************************************************************
  Initialize the shuffled players list (as from a loaded savegame).
**************************************************************************/
void set_shuffled_players(int *shuffled_players)
{
  int i;

  log_debug("set_shuffled_players: loading shuffled array %p",
            shuffled_players);

  for (i = 0; i < player_slot_count(); i++) {
    shuffled_order[i] = shuffled_players[i];
    log_debug("shuffled_order[%d] = %d", i, shuffled_order[i]);
  }
}

/**************************************************************************
  Returns the i'th shuffled player, or NULL.

  NB: You should never need to call this function directly.
**************************************************************************/
struct player *shuffled_player(int i)
{
  struct player *pplayer;

  pplayer = player_by_number(shuffled_order[i]);
  log_debug("shuffled_player(%d) = %d (%s)",
            i, shuffled_order[i], player_name(pplayer));
  return pplayer;
}

/****************************************************************************
  This function returns a random-ish nation that is suitable for 'barb_type'
  and is usable (not already in use by an existing player, and if
  only_available is set, has its 'available' bit set).

  Unless 'ignore_conflicts' is set, this function tries hard to avoid a
  nation marked as "conflicting with" one already in the game. A
  conflicting nation will be returned only if the alternative is to return
  NO_NATION_SELECTED. Such a return indicates that there are no remaining
  nations which match the above criteria.

  If 'choices' is non-NULL, nations from the supplied list are preferred;
  but if there are no (non-conflicting) nations on the list that match the
  criteria, one will be chosen from outside the list (as if the list had
  not been supplied).

  All other things being equal, prefers to pick a nation which returns a
  high score from nations_match() relative to any nations already in the
  game.
****************************************************************************/
struct nation_type *pick_a_nation(const struct nation_list *choices,
                                  bool ignore_conflicts,
                                  bool only_available,
                                  enum barbarian_type barb_type)
{
  enum {
    UNAVAILABLE, AVAILABLE, PREFERRED, UNWANTED
  } nations_used[nation_count()], looking_for;
  int match[nation_count()], pick, index;
  int num_avail_nations = 0, num_pref_nations = 0;

  /* Values of nations_used:
   * UNAVAILABLE - nation is already used or is a special nation.
   * AVAILABLE - we can use this nation.
   * PREFERRED - we can use this nation and it is on the choices list.
   * UNWANTED - we can use this nation, but we really don't want to. */
  nations_iterate(pnation) {
    index = nation_index(pnation);

    if (pnation->player
        || (only_available && !pnation->is_available)
        || (barb_type != nation_barbarian_type(pnation))
        || (barb_type == NOT_A_BARBARIAN && !is_nation_playable(pnation))) {
      /* Nation is unplayable or already used: don't consider it. */
      nations_used[index] = UNAVAILABLE;
      match[index] = 0;
      continue;
    }

    if (get_allowed_nation_groups()) {
      /* Don't consider any nations outside the set of allowed groups. */
      bool allowed = FALSE;
      nation_group_list_iterate(get_allowed_nation_groups(), pgroup) {
        if (nation_is_in_group(pnation, pgroup)) {
          allowed = TRUE;
          break;
        }
      } nation_group_list_iterate_end;
      if (!allowed) {
        nations_used[index] = UNAVAILABLE;
        match[index] = 0;
        continue;
      }
    }

    nations_used[index] = AVAILABLE;

    /* Determine which nations look good with nations already in the game,
     * or conflict with them. */
    match[index] = 1;
    players_iterate(pplayer) {
      if (pplayer->nation != NO_NATION_SELECTED) {
        int x = nations_match(pnation, nation_of_player(pplayer),
                              ignore_conflicts);
        if (x < 0) {
          log_debug("Nations '%s' (nb %d) and '%s' (nb %d) are in conflict.",
                    nation_rule_name(pnation), nation_number(pnation),
                    nation_rule_name(nation_of_player(pplayer)),
                    nation_number(nation_of_player(pplayer)));
          nations_used[index] = UNWANTED;
          match[index] -= x * 100;
          break;
        } else {
          match[index] += x * 100;
        }
      }
    } players_iterate_end;

    if (AVAILABLE == nations_used[index]) {
      num_avail_nations += match[index];
    }
  } nations_iterate_end;

  /* Mark as preferred those nations which are on the choices list and
   * which are AVAILABLE, but no UNWANTED */
  if (NULL != choices) {
    nation_list_iterate(choices, pnation) {
      index = nation_index(pnation);
      if (nations_used[index] == AVAILABLE) {
        num_pref_nations += match[index];
        nations_used[index] = PREFERRED;
      }
    } nation_list_iterate_end;
  }

  if (0 < num_pref_nations || 0 < num_avail_nations) {
    if (0 < num_pref_nations) {
      /* Use a preferred nation only. */
      pick = fc_rand(num_pref_nations);
      looking_for = PREFERRED;
      log_debug("Picking a preferred nation.");
    } else {
      /* Use any available nation. */
      fc_assert(0 < num_avail_nations);
      pick = fc_rand(num_avail_nations);
      looking_for = AVAILABLE;
      log_debug("Picking an available nation.");
    }

    nations_iterate(pnation) {
      index = nation_index(pnation);
      if (nations_used[index] == looking_for) {
        pick -= match[index];

        if (0 > pick) {
          return pnation;
        }
      }
    } nations_iterate_end;
  } else {
    /* No available nation: use unwanted nation... */
    struct nation_type *less_worst_nation = NO_NATION_SELECTED;
    int less_worst_score = -FC_INFINITY;

    log_debug("Picking an unwanted nation.");
    nations_iterate(pnation) {
      index = nation_index(pnation);
      if (UNWANTED == nations_used[index]) {
        pick = -fc_rand(match[index]);
        if (pick > less_worst_score) {
          less_worst_nation = pnation;
          less_worst_score = pick;
        }
      }
    } nations_iterate_end;

    if (NO_NATION_SELECTED != less_worst_nation) {
      return less_worst_nation;
    }
  }

  /* If we get this far and a restriction to nation set(s) is in force,
   * _permanently_ remove the restriction and try again (recursively,
   * but it can only happen once per game).
   * This should get us a nation if possible, and have the side-effect that
   * future picked nations won't honor the restrictions.
   * (This is dirty; it would be better to have prevented attempts to
   * create more players than the restrictions permit via playable_nations
   * or similar.) */
  if (get_allowed_nation_groups()) {
    log_verbose("Unable to honor restricted nation set(s). Removing "
                "restrictions for the rest of the game.");
    set_allowed_nation_groups(NULL);  /* no restrictions */
    return pick_a_nation(choices, ignore_conflicts, only_available, barb_type);
  }

  log_error("No nation found!");
  return NO_NATION_SELECTED;
}

/****************************************************************************
  Called when something is changed; this resets everyone's readiness.
****************************************************************************/
void reset_all_start_commands(void)
{
  if (S_S_INITIAL != server_state()) {
    return;
  }
  players_iterate(pplayer) {
    if (pplayer->is_ready) {
      pplayer->is_ready = FALSE;
      send_player_info_c(pplayer, game.est_connections);
    }
  } players_iterate_end;
}

/**********************************************************************
This function creates a new player and copies all of it's science
research etc.  Players are both thrown into anarchy and gold is
split between both players.
                               - Kris Bubendorfer 
***********************************************************************/
static struct player *split_player(struct player *pplayer)
{
  struct player_research *new_research, *old_research;
  struct player *cplayer;

  /* make a new player, or not */
  cplayer = server_create_player(-1, default_ai_type_name(), NULL);
  if (!cplayer) {
    return NULL;
  }
  server_player_init(cplayer, TRUE, TRUE);

  /* Rebel will always be an AI player */
  player_set_nation(cplayer, pick_a_nation
      (nation_of_player(pplayer)->server.civilwar_nations,
       TRUE, FALSE, NOT_A_BARBARIAN));
  server_player_set_name(cplayer,
                         pick_random_player_name(nation_of_player(cplayer)));

  /* Send information about the used player slot to all connections. */
  send_player_info_c(cplayer, NULL);

  sz_strlcpy(cplayer->username, ANON_USER_NAME);
  cplayer->is_connected = FALSE;
  cplayer->government = nation_of_player(cplayer)->init_government;
  fc_assert(cplayer->revolution_finishes < 0);
  /* No capital for the splitted player. */
  cplayer->server.capital = FALSE;

  players_iterate(other_player) {
    struct player_diplstate *ds_co
      = player_diplstate_get(cplayer, other_player);
    struct player_diplstate *ds_oc
      = player_diplstate_get(other_player, cplayer);

    if (get_player_bonus(other_player, EFT_NO_DIPLOMACY)) {
      ds_co->type = DS_WAR;
      ds_oc->type = DS_WAR;
    } else {
      ds_co->type = DS_NO_CONTACT;
      ds_oc->type = DS_NO_CONTACT;
    }

    ds_co->has_reason_to_cancel = 0;
    ds_co->turns_left = 0;
    ds_co->contact_turns_left = 0;
    ds_oc->has_reason_to_cancel = 0;
    ds_oc->turns_left = 0;
    ds_oc->contact_turns_left = 0;

    /* Send so that other_player sees updated diplomatic info;
     * pplayer will be sent later anyway
     */
    if (other_player != pplayer) {
      send_player_all_c(other_player, other_player->connections);
    }
  } players_iterate_end;

  /* Split the resources */
  cplayer->economic.gold = pplayer->economic.gold;
  cplayer->economic.gold /= 2;
  pplayer->economic.gold -= cplayer->economic.gold;

  /* Copy the research */
  new_research = player_research_get(cplayer);
  old_research = player_research_get(pplayer);

  new_research->bulbs_researched = 0;
  new_research->techs_researched = old_research->techs_researched;
  new_research->researching = old_research->researching;
  new_research->tech_goal = old_research->tech_goal;

  advance_index_iterate(A_NONE, i) {
    new_research->inventions[i] = old_research->inventions[i];
  } advance_index_iterate_end;
  cplayer->phase_done = TRUE; /* Have other things to think
				 about - paralysis */
  BV_CLR_ALL(cplayer->real_embassy);   /* all embassies destroyed */

  /* Do the ai */

  cplayer->ai_controlled = TRUE;
  cplayer->ai_common.maxbuycost = pplayer->ai_common.maxbuycost;
  cplayer->ai_common.handicaps = pplayer->ai_common.handicaps;
  cplayer->ai_common.warmth = pplayer->ai_common.warmth;
  cplayer->ai_common.frost = pplayer->ai_common.frost;
  set_ai_level_direct(cplayer, game.info.skill_level);

  advance_index_iterate(A_NONE, i) {
    cplayer->ai_common.tech_want[i] = pplayer->ai_common.tech_want[i];
  } advance_index_iterate_end;
  
  /* change the original player */
  if (government_of_player(pplayer) != game.government_during_revolution) {
    pplayer->target_government = pplayer->government;
    pplayer->government = game.government_during_revolution;
    pplayer->revolution_finishes = game.info.turn + 1;
  }
  player_research_get(pplayer)->bulbs_researched = 0;
  BV_CLR_ALL(pplayer->real_embassy);   /* all embassies destroyed */

  /* give splitted player the embassies to his team mates back, if any */
  if (pplayer->team) {
    players_iterate(pdest) {
      if (pplayer->team == pdest->team
          && pplayer != pdest) {
        establish_embassy(pplayer, pdest);
      }
    } players_iterate_end;
  }

  pplayer->economic = player_limit_to_max_rates(pplayer);

  /* copy the maps */

  give_map_from_player_to_player(pplayer, cplayer);

  /* Not sure if this is necessary, but might be a good idea
   * to avoid doing some ai calculations with bogus data. */
  adv_data_phase_init(cplayer, TRUE);
  CALL_PLR_AI_FUNC(phase_begin, cplayer, cplayer, TRUE);
  CALL_PLR_AI_FUNC(gained_control, cplayer, cplayer);
  if (pplayer->ai_controlled) {
    CALL_PLR_AI_FUNC(split_by_civil_war, pplayer, pplayer);
  }

  return cplayer;
}

/**************************************************************************
  Check if civil war is possible for a player.
  If conquering_city is TRUE, one of the cities currently in the empire
  will shortly not be and shouldn't be considered.
  honour_server_option controls whether we honour the 'civilwarsize'
  server option. (If we don't, we still enforce a minimum empire size, to
  avoid the risk of creating a new player with no cities.)
**************************************************************************/
bool civil_war_possible(struct player *pplayer, bool conquering_city,
                        bool honour_server_option)
{
  int n = city_list_size(pplayer->cities);

  if (n - (conquering_city?1:0) < GAME_MIN_CIVILWARSIZE) {
    return FALSE;
  }
  if (honour_server_option) {
    return game.server.civilwarsize < GAME_MAX_CIVILWARSIZE
           && n >= game.server.civilwarsize;
  } else {
    return TRUE;
  }
}

/********************************************************************** 
civil_war_triggered:
 * The capture of a capital is not a sure fire way to throw
and empire into civil war.  Some governments are more susceptible 
than others, here are the base probabilities:
Anarchy   	90%
Despotism 	80%
Monarchy  	70%
Fundamentalism  60% (Only in civ2 ruleset)
Communism 	50%
Republic  	40%
Democracy 	30%
 * In addition each city in disorder adds 5%, each celebrating city
subtracts 5% from the probability of a civil war.  
 * If you have at least 1 turns notice of the impending loss of 
your capital, you can hike luxuries up to the hightest value,
and by this reduce the chance of a civil war.  In fact by
hiking the luxuries to 100% under Democracy, it is easy to
get massively negative numbers - guaranteeing imunity from
civil war.  Likewise, 3 cities in disorder under despotism
guarantees a civil war.
 * This routine calculates these probabilities and returns true
if a civil war is triggered.
                                   - Kris Bubendorfer 
***********************************************************************/
bool civil_war_triggered(struct player *pplayer)
{
  /* Get base probabilities */

  int dice = fc_rand(100); /* Throw the dice */
  int prob = get_player_bonus(pplayer, EFT_CIVIL_WAR_CHANCE);

  /* Now compute the contribution of the cities. */
  
  city_list_iterate(pplayer->cities, pcity)
    if (city_unhappy(pcity)) {
      prob += 5;
    }
    if (city_celebrating(pcity)) {
      prob -= 5;
    }
  city_list_iterate_end;

  log_verbose("Civil war chance for %s: prob %d, dice %d",
              player_name(pplayer), prob, dice);
  
  return (dice < prob);
}

/**********************************************************************
Capturing a nation's capital is a devastating blow.  This function
creates a new AI player, and randomly splits the original players
city list into two.  Of course this results in a real mix up of 
teritory - but since when have civil wars ever been tidy, or civil.

Embassies:  All embassies with other players are lost.  Other players
            retain their embassies with pplayer.
 * Units:      Units inside cities are assigned to the new owner
            of the city.  Units outside are transferred along 
            with the ownership of their supporting city.
            If the units are in a unit stack with non rebel units,
            then whichever units are nearest an allied city
            are teleported to that city.  If the stack is a 
            transport at sea, then all rebel units on the 
            transport are teleported to their nearest allied city.

Cities:     Are split randomly into 2.  This results in a real
            mix up of teritory - but since when have civil wars 
            ever been tidy, or for any matter civil?
 *
One caveat, since the spliting of cities is random, you can
conceive that this could result in either the original player
or the rebel getting 0 cities.  To prevent this, the hack below
ensures that each side gets roughly half, which ones is still 
determined randomly.
                                   - Kris Bubendorfer
***********************************************************************/
struct player *civil_war(struct player *pplayer)
{
  int i, j;
  struct player *cplayer;

  /* It is possible that this function gets called after pplayer
   * died. Player pointers are safe even after death. */
  if (!pplayer->is_alive) {
    return NULL;
  }

  if (player_count() >= MAX_NUM_PLAYERS) {
    /* No space to make additional player */
    log_normal(_("Could not throw %s into civil war - too many players"),
               nation_plural_for_player(pplayer));
    return NULL;
  }
  if (normal_player_count() >= server.playable_nations) {
    /* No nation for additional player */
    log_normal(_("Could not throw %s into civil war - no available nations"),
               nation_plural_for_player(pplayer));
    return NULL;
  }

  if (player_count() == game.server.max_players) {
    /* 'maxplayers' must be increased to allow for a new player. */

    /* This assert should never be called due to the first check above. */
    fc_assert_ret_val(game.server.max_players < MAX_NUM_PLAYERS, NULL);

    game.server.max_players++;
    log_debug("Increase 'maxplayers' to allow the creation of a new player "
              "due to civil war.");
  }

  cplayer = split_player(pplayer);

  /* Before units, cities, so clients know name of new nation
   * (for debugging etc).
   */
  send_player_all_c(cplayer,  NULL);
  send_player_all_c(pplayer,  NULL);

  /* Now split the empire */

  log_verbose("%s civil war; created AI %s",
              nation_rule_name(nation_of_player(pplayer)),
              nation_rule_name(nation_of_player(cplayer)));
  notify_player(pplayer, NULL, E_CIVIL_WAR, ftc_server,
                _("Your nation is thrust into civil war."));

  notify_player(pplayer, NULL, E_FIRST_CONTACT, ftc_server,
                /* TRANS: <leader> ... the Poles. */
                _("%s is the rebellious leader of the %s."),
                player_name(cplayer),
                nation_plural_for_player(cplayer));

  j = city_list_size(pplayer->cities);	    /* number left to process */
  /* It doesn't make sense to try to split an empire of 1 city.
   * This should have been enforced by civil_war_possible(). */
  fc_assert(j >= 2);
  /* Number to try to flip; ensure that at least one non-capital city is
   * flipped */
  i = MAX(city_list_size(pplayer->cities)/2,
          1 + (player_capital(pplayer) != NULL));
  city_list_iterate(pplayer->cities, pcity) {
    if (!is_capital(pcity)) {
      if (i >= j || (i > 0 && fc_rand(2) == 1)) {
        /* Transfer city and units supported by this city to the new owner.
         * We do NOT resolve stack conflicts here, but rather later.
         * Reason: if we have a transporter from one city which is carrying
         * a unit from another city, and both cities join the rebellion. We
         * resolved stack conflicts for each city we would teleport the first
         * of the units we met since the other would have another owner. */
        transfer_city(cplayer, pcity, -1, FALSE, FALSE, FALSE);
        log_verbose("%s declares allegiance to the %s.", city_name(pcity),
                    nation_rule_name(nation_of_player(cplayer)));
        notify_player(pplayer, pcity->tile, E_CITY_LOST, ftc_server,
                      /* TRANS: <city> ... the Poles. */
                      _("%s declares allegiance to the %s."),
                      city_link(pcity),
                      nation_plural_for_player(cplayer));
        i--;
      }
    }
    j--;
  } city_list_iterate_end;

  resolve_unit_stacks(pplayer, cplayer, FALSE);

  i = city_list_size(cplayer->cities);
  fc_assert(i > 0); /* rebels should have got at least one city */

  /* Choose a capital (random). */
  city_build_free_buildings(city_list_get(cplayer->cities, fc_rand(i)));

  notify_player(NULL, NULL, E_CIVIL_WAR, ftc_server,
                /* TRANS: ... Danes ... Poles ... <7> cities. */
                PL_("Civil war partitions the %s;"
                    " the %s now hold %d city.",
                    "Civil war partitions the %s;"
                    " the %s now hold %d cities.",
                    i),
                nation_plural_for_player(pplayer),
                nation_plural_for_player(cplayer),
                i);

  return cplayer;
}

/**************************************************************************
 The client has send as a chunk of the attribute block.
**************************************************************************/
void handle_player_attribute_chunk(struct player *pplayer,
                                   const struct packet_player_attribute_chunk
                                   *chunk)
{
  generic_handle_player_attribute_chunk(pplayer, chunk);
}

/**************************************************************************
 The client request an attribute block.
**************************************************************************/
void handle_player_attribute_block(struct player *pplayer)
{
  send_attribute_block(pplayer, pplayer->current_conn);
}

/**************************************************************************
...
(Hmm, how should "turn done" work for multi-connected non-observer players?)
**************************************************************************/
void handle_player_phase_done(struct player *pplayer,
			      int turn)
{
  if (turn != game.info.turn) {
    /* If this happens then the player actually pressed turn-done on a
     * previous turn but we didn't receive it until now.  The player
     * probably didn't actually mean to end their turn! */
    return;
  }
  pplayer->phase_done = TRUE;

  check_for_full_turn_done();

  send_player_all_c(pplayer, NULL);
}

/**************************************************************************
  Return the number of barbarian players.
**************************************************************************/
int barbarian_count(void)
{
  return server.nbarbarians;
}

/**************************************************************************
  Return the number of non-barbarian players.
**************************************************************************/
int normal_player_count(void)
{
  return player_count() - server.nbarbarians;
}

/****************************************************************************
  Add a status flag to a player.
****************************************************************************/
void player_status_add(struct player *plr, enum player_status pstatus)
{
  BV_SET(plr->server.status, pstatus);
}

/****************************************************************************
  Add a status flag to a player.
****************************************************************************/
bool player_status_check(struct player *plr, enum player_status pstatus)
{
  return BV_ISSET(plr->server.status, pstatus);
}

/****************************************************************************
  Reset player status to 'normal'.
****************************************************************************/
void player_status_reset(struct player *plr)
{
  BV_CLR_ALL(plr->server.status);
  player_status_add(plr, PSTATUS_NORMAL);
}

/**************************************************************************
  Returns the username the control of the player is delegated to.
**************************************************************************/
const char *player_delegation_get(const struct player *pplayer)
{
  if (pplayer == NULL || strlen(pplayer->server.delegate_to) == 0) {
    /* No delegation if there is no player. */
    return NULL;
  } else {
    return pplayer->server.delegate_to;
  }
}

/**************************************************************************
  Define a delegation.
**************************************************************************/
void player_delegation_set(struct player *pplayer, const char *username)
{
  fc_assert_ret(pplayer != NULL);

  if (username == NULL || strlen(username) == 0) {
    pplayer->server.delegate_to[0] = '\0';
  } else {
    sz_strlcpy(pplayer->server.delegate_to, username);
  }
}

/*****************************************************************************
 Returns TRUE if a delegation is active.
*****************************************************************************/
bool player_delegation_active(const struct player *pplayer)
{
  return (pplayer && strlen(pplayer->server.orig_username) != 0);
}

/*****************************************************************************
 Send information about delegations.
*****************************************************************************/
void send_delegation_info(const struct connection *pconn)
{
  if (game.info.is_new_game
      || !pconn->playing) {
    return;
  }

  if (player_delegation_get(pconn->playing) != NULL) {
    notify_conn(pconn->self, NULL, E_CONNECTION, ftc_server,
                _("Delegation to user '%s' defined."),
                player_delegation_get(pconn->playing));
  }

  players_iterate(aplayer) {
    if (player_delegation_get(aplayer) != NULL
        && strcmp(player_delegation_get(aplayer),
                  pconn->playing->username) == 0) {
      notify_conn(pconn->self, NULL, E_CONNECTION, ftc_server,
                  _("Control of player '%s' delegated to you."),
                  player_name(aplayer));
    }
  } players_iterate_end;
}

/*****************************************************************************
  Check for a playe in delegated state by its user name. See also
  player_by_user().
*****************************************************************************/
struct player *player_by_user_delegated(const char *name)
{
  players_iterate(pplayer) {
    if (fc_strcasecmp(name, pplayer->server.orig_username) == 0) {
      return pplayer;
    }
  } players_iterate_end;

  return NULL;
}

/****************************************************************************
  Initialise the player colors.
****************************************************************************/
void playercolor_init(void)
{
  fc_assert_ret(game.server.plr_colors == NULL);
  game.server.plr_colors = rgbcolor_list_new();
}

/****************************************************************************
  Free the memory allocated for the player color.
****************************************************************************/
void playercolor_free(void)
{
  if (game.server.plr_colors == NULL) {
    return;
  }

  if (rgbcolor_list_size(game.server.plr_colors) > 0) {
    rgbcolor_list_iterate(game.server.plr_colors, prgbcolor) {
      rgbcolor_list_remove(game.server.plr_colors, prgbcolor);
      rgbcolor_destroy(prgbcolor);
    } rgbcolor_list_iterate_end;
  };
  rgbcolor_list_destroy(game.server.plr_colors);
  game.server.plr_colors = NULL;
}

/****************************************************************************
  Add a color to the list of all available player colors.
****************************************************************************/
void playercolor_add(struct rgbcolor *prgbcolor)
{
  fc_assert_ret(game.server.plr_colors != NULL);

  rgbcolor_list_append(game.server.plr_colors, prgbcolor);
}

/****************************************************************************
  Get the player color with the index 'id'.
****************************************************************************/
struct rgbcolor *playercolor_get(int id)
{
  fc_assert_ret_val(game.server.plr_colors != NULL, NULL);

  return rgbcolor_list_get(game.server.plr_colors, id);
}

/****************************************************************************
  Number of player colors defined.
****************************************************************************/
int playercolor_count(void)
{
  fc_assert_ret_val(game.server.plr_colors != NULL, -1);

  return rgbcolor_list_size(game.server.plr_colors);
}
