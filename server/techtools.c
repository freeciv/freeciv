/********************************************************************** 
 Freeciv - Copyright (C) 2005 The Freeciv Team
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
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "shared.h"
#include "support.h"

/* common */
#include "game.h"
#include "government.h"
#include "movement.h"
#include "player.h"
#include "research.h"
#include "tech.h"
#include "unit.h"

/* common/scriptcore */
#include "luascript_types.h"

/* server */
#include "citytools.h"
#include "cityturn.h"
#include "connecthand.h"
#include "gamehand.h"
#include "maphand.h"
#include "notify.h"
#include "plrhand.h"
#include "unittools.h"

/* server/scripting */
#include "script_server.h"

#include "techtools.h"

/* Define this to add information about tech upkeep. */
#undef TECH_UPKEEP_DEBUGGING

static Tech_type_id pick_random_tech_to_lose(struct player* plr);
static void player_tech_lost(struct player* plr, Tech_type_id tech);
static void forget_tech_transfered(struct player *pplayer, Tech_type_id tech);

/**************************************************************************
  Reduce dipl cost bulbs from player.
**************************************************************************/
void do_dipl_cost(struct player *pplayer, Tech_type_id tech)
{
  struct research * research = research_get(pplayer);

  research->bulbs_researched -=
      (research_total_bulbs_required(research, tech, FALSE)
       * game.server.diplcost) / 100;
  research->researching_saved = A_UNKNOWN;
}

/**************************************************************************
  Reduce free cost bulbs from player.
**************************************************************************/
void do_free_cost(struct player *pplayer, Tech_type_id tech)
{
  struct research * research = research_get(pplayer);

  research->bulbs_researched -=
      (research_total_bulbs_required(research, tech, FALSE)
       * game.server.freecost) / 100;
  research->researching_saved = A_UNKNOWN;
}

/**************************************************************************
  Reduce conquer cost bulbs from player.
**************************************************************************/
void do_conquer_cost(struct player *pplayer, Tech_type_id tech)
{
  struct research * research = research_get(pplayer);

  research->bulbs_researched -=
      (research_total_bulbs_required(research, tech, FALSE)
       * game.server.conquercost) / 100;
  research->researching_saved = A_UNKNOWN;
}

/****************************************************************************
  Player has researched a new technology
****************************************************************************/
static void tech_researched(struct player *plr)
{
  struct research *research = research_get(plr);
  /* plr will be notified when new tech is chosen */

  /* FIXME: We should notify all embassies with all players sharing the
   * research. */
  notify_embassies(plr, NULL, NULL, E_TECH_GAIN, ftc_server,
                   _("The %s have researched %s."),
                   nation_plural_for_player(plr),
                   research_advance_name_translation(research,
                                                     research->researching));

  /* Deduct tech cost */
  research->bulbs_researched = (research->bulbs_researched
                                - research->researching_cost);

  /* cache researched technology for event signal, because found_new_tech() changes the research goal */
  Tech_type_id researched_tech = research->researching;

  /* do all the updates needed after finding new tech */
  found_new_tech(plr, research->researching, TRUE, TRUE);

  script_server_signal_emit("tech_researched", 3,
                            API_TYPE_TECH_TYPE,
                            advance_by_number(researched_tech),
                            API_TYPE_PLAYER, plr,
                            API_TYPE_STRING, "researched");
}

/****************************************************************************
  Give technologies to players with EFT_TECH_PARASITE (traditionally from
  the Great Library).
****************************************************************************/
void do_tech_parasite_effect(struct player *pplayer)
{
  struct research *presearch = research_get(pplayer);
  int mod;
  struct effect_list *plist = effect_list_new();

  /* Note that two EFT_TECH_PARASITE effects will combine into a single,
   * much worse effect. */
  if ((mod = get_player_bonus_effects(plist, pplayer,
				      EFT_TECH_PARASITE)) > 0) {
    char buf[512];

    buf[0] = '\0';
    effect_list_iterate(plist, peffect) {
      if (buf[0] != '\0') {
	sz_strlcat(buf, ", ");
      }
      get_effect_req_text(peffect, buf, sizeof(buf));
    } effect_list_iterate_end;

    advance_index_iterate(A_FIRST, i) {
      if (research_invention_gettable(presearch, i,
                                      game.info.tech_parasite_allow_holes)
          && research_invention_state(presearch, i) != TECH_KNOWN) {
        int num_research = 0;

        researches_iterate(presearch) {
          if (presearch->inventions[i].state == TECH_KNOWN) {
            num_research++;
          }
        } researches_iterate_end;
        if (num_research >= mod) {
          notify_player(pplayer, NULL, E_TECH_GAIN, ftc_server,
                        _("%s acquired from %s!"),
                        research_advance_name_translation(presearch, i),
                        buf);
          notify_embassies(pplayer, NULL, NULL, E_TECH_GAIN, ftc_server,
                           _("The %s have acquired %s from %s."),
                           nation_plural_for_player(pplayer),
                           research_advance_name_translation(presearch, i),
                           buf);

	  do_free_cost(pplayer, i);
	  found_new_tech(pplayer, i, FALSE, TRUE);

          script_server_signal_emit("tech_researched", 3,
                                    API_TYPE_TECH_TYPE,
                                    advance_by_number(i),
                                    API_TYPE_PLAYER, pplayer,
                                    API_TYPE_STRING, "stolen");
	  break;
	}
      }
    } advance_index_iterate_end;
  }
  effect_list_destroy(plist);
}

/****************************************************************************
  Fill packet fields. Helper for following functions.
****************************************************************************/
static inline void
package_research_info(struct packet_research_info *packet,
                      const struct research *presearch)
{
  packet->id = research_number(presearch);
  packet->techs_researched = presearch->techs_researched;
  packet->future_tech = presearch->future_tech;
  packet->researching = presearch->researching;
  packet->researching_cost = presearch->researching_cost;
  packet->bulbs_researched = presearch->bulbs_researched;
  packet->tech_goal = presearch->tech_goal;
  advance_index_iterate(A_NONE, i) {
    packet->inventions[i] = presearch->inventions[i].state + '0';
  } advance_index_iterate_end;
  packet->inventions[advance_count()] = '\0';
  packet->tech_goal = presearch->tech_goal;
#ifdef DEBUG
  log_verbose("Research nb %d inventions: %s",
              research_number(presearch),
              packet->inventions);
#endif
}

/****************************************************************************
  Send the research packet info to player sharing the research and global
  observers.
****************************************************************************/
static void send_research_info_to_owners(const struct research *presearch)
{
  struct packet_research_info packet;

  /* Packaging. */
  package_research_info(&packet, presearch);

  /* Send to players sharing the research. */
  research_players_iterate(presearch, pplayer) {
    lsend_packet_research_info(pplayer->connections, &packet);
  } research_players_iterate_end;

  /* Send to global observers. */
  conn_list_iterate(game.est_connections, pconn) {
    if (conn_is_global_observer(pconn)) {
      send_packet_research_info(pconn, &packet);
    }
  } conn_list_iterate_end;
}

/****************************************************************************
  Send research info for 'presearch' to 'dest'. 'dest' can be NULL to send
  to all established connections.
****************************************************************************/
void send_research_info(const struct research *presearch,
                        const struct conn_list *dest)
{
  struct packet_research_info full_info, restricted_info;
  const struct player *pplayer;
  bool embassy;

  fc_assert_ret(NULL != presearch);
  if (NULL == dest) {
    dest = game.est_connections;
  }

  /* Packaging */
  package_research_info(&full_info, presearch);
  restricted_info = full_info;
  restricted_info.tech_goal = A_UNSET;

  conn_list_iterate(dest, pconn) {
    pplayer = conn_get_player(pconn);
    if (NULL != pplayer) {
      if (presearch == research_get(pplayer)) {
        /* Case research owner. */
        send_packet_research_info(pconn, &full_info);
      } else {
        /* 'pconn' may have an embassy for looking to 'presearch'. */
        embassy = FALSE;
        player_list_iterate(team_members(pplayer->team), member) {
          research_players_iterate(presearch, powner) {
            if (player_has_embassy(member, powner)) {
              embassy = TRUE;
              break;
            }
          } research_players_iterate_end;
          if (embassy) {
            send_packet_research_info(pconn, &restricted_info);
            break;
          }
        } player_list_iterate_end;
      }
    } else if (pconn->observer) {
      /* Case global observer. */
      send_packet_research_info(pconn, &full_info);
    }
  } conn_list_iterate_end;
}

/****************************************************************************
  Fill the array which contains information about which government
  the player can switch to.
****************************************************************************/
static void fill_can_switch_to_government_array(struct player* plr, bool* can_switch)
{
  governments_iterate(gov) {
    /* We do it this way so all requirements are checked, including
     * statue-of-liberty effects. */
    can_switch[government_index(gov)] = can_change_to_government(plr, gov);
  } governments_iterate_end;
} 

/****************************************************************************
  Fill the array which contains information about value of the
  EFT_HAVE_EMBASSIES effect for each player
****************************************************************************/
static void fill_have_embassies_array(int* have_embassies)
{
  players_iterate(aplr) {
    have_embassies[player_index(aplr)]
      = get_player_bonus(aplr, EFT_HAVE_EMBASSIES);
  } players_iterate_end;
}

/****************************************************************************
  Player has a new technology (from somewhere). was_discovery is passed 
  on to upgrade_city_extras. Logging & notification is not done here as 
  it depends on how the tech came. If next_tech is other than A_NONE, this 
  is the next tech to research.
****************************************************************************/
void found_new_tech(struct player *plr, Tech_type_id tech_found,
		    bool was_discovery, bool saving_bulbs)
{
  bool bonus_tech_hack = FALSE;
  bool was_first = FALSE;
  int had_embassies[player_slot_count()];
  struct city *pcity;
  bool could_switch[government_count()];
  struct research *research = research_get(plr);
  struct advance *vap = valid_advance_by_number(tech_found);
  struct packet_tech_gained packet;

  /* HACK: A_FUTURE doesn't "exist" and is thus not "available".  This may
   * or may not be the correct thing to do.  For these sanity checks we
   * just special-case it. */
  fc_assert_ret(tech_found == A_FUTURE
                || (vap && research_invention_state(research, tech_found)
                    != TECH_KNOWN));

  /* got_tech allows us to change research without applying techpenalty
   * (without losing bulbs) */
  if (tech_found == research->researching) {
    research->got_tech = TRUE;
  }
  research->researching_saved = A_UNKNOWN;
  research->techs_researched++;
  was_first = (!game.info.global_advances[tech_found]);

  if (was_first && vap) {
    /* Alert the owners of any wonders that have been made obsolete */
    improvement_iterate(pimprove) {
      requirement_vector_iterate(&pimprove->obsolete_by, pobs) {
        if (pobs->source.kind == VUT_ADVANCE
            && pobs->source.value.advance == vap
            && pobs->range >= REQ_RANGE_WORLD
            && is_great_wonder(pimprove)
            && (pcity = city_from_great_wonder(pimprove))) {
          notify_player(city_owner(pcity), NULL, E_WONDER_OBSOLETE, ftc_server,
                        _("Discovery of %s OBSOLETES %s in %s!"), 
                        research_advance_name_translation
                            (research_get(city_owner(pcity)), tech_found),
                        improvement_name_translation(pimprove),
                        city_link(pcity));
        }
      } requirement_vector_iterate_end;
    } improvement_iterate_end;
  }

  if (was_first && tech_found != A_FUTURE
   && advance_has_flag(tech_found, TF_BONUS_TECH)) {
    bonus_tech_hack = TRUE;
  }
  
  /* Count EFT_HAVE_EMBASSIES effect for each player.
   * We will check what has changed later */
  fill_have_embassies_array(had_embassies);

  /* Memorize some values before the tech is marked as researched.
   * They will be used to notify a player about a change */
  fill_can_switch_to_government_array(plr, could_switch);

  /* Mark the tech as known in the research struct and update
   * global_advances array */
  if (!is_future_tech(tech_found)) {
    research_invention_set(research, tech_found, TECH_KNOWN);
    research_update(research);
  }

  /* Make proper changes for all players sharing the research */  
  research_players_iterate(research, aplayer) {
    remove_obsolete_buildings(aplayer);

    /* Give free infrastructure in every city */
    if (tech_found != A_FUTURE) {
      upgrade_all_city_extras(aplayer, was_discovery);
    }

    /* Enhance vision of units if a player-ranged effect has changed. Note
     * that world-ranged effects will not be updated immediately. */
    unit_list_refresh_vision(aplayer->units);
  } research_players_iterate_end;

  /* Notify a player about new governments available */
  governments_iterate(gov) {
    if (!could_switch[government_index(gov)]
        && can_change_to_government(plr, gov)) {
      notify_research(plr, E_NEW_GOVERNMENT, ftc_server,
                      _("Discovery of %s makes the government form %s"
                        " available. You may want to start a revolution."),
                      research_advance_name_translation(research, tech_found),
                      government_name_translation(gov));
    }
  } governments_iterate_end;

  /* Inform players about their new tech. */
  send_research_info(research, NULL);

  if (tech_found == research->tech_goal) {
    research->tech_goal = A_UNSET;
  }

  if (tech_found == research->researching) {
    /* Try to pick new tech to research. */
    Tech_type_id next_tech = research_goal_step(research,
                                                research->tech_goal);

    /* As this function can be recursive, we need to print the messages
     * before really picking the new technology. */
    if (A_UNSET != next_tech) {
      notify_research(plr, E_TECH_LEARNED, ftc_server,
                      _("Learned %s. Our scientists focus on %s; "
                        "goal is %s."),
                      research_advance_name_translation(research,
                                                        tech_found),
                      research_advance_name_translation(research,
                                                        next_tech),
                      research_advance_name_translation(research,
                                                        research->tech_goal));
    } else {
      if (plr->ai_controlled) {
        next_tech = pick_random_tech(plr);
      } else if (is_future_tech(tech_found)) {
        /* Continue researching future tech. */
        next_tech = A_FUTURE;
      } else {
        next_tech = A_UNSET;
      }

      if (A_UNSET == next_tech) {
        notify_research(plr, E_TECH_LEARNED, ftc_server,
                        _("Learned %s. Scientists "
                          "do not know what to research next."),
                        research_advance_name_translation(research,
                                                          tech_found));
      } else if (!is_future_tech(next_tech) || !is_future_tech(tech_found)) {
        notify_research(plr, E_TECH_LEARNED, ftc_server,
                        _("Learned %s. Scientists choose to research %s."),
                        research_advance_name_translation(research,
                                                          tech_found),
                        research_advance_name_translation(research,
                                                          next_tech));
      } else {
        char buffer1[300], buffer2[300];

        /* FIXME: Handle the translation in a single string. */
        fc_snprintf(buffer1, sizeof(buffer1), _("Learned %s. "),
                    research_advance_name_translation(research, tech_found));
        research->future_tech++;
        fc_snprintf(buffer2, sizeof(buffer2), _("Researching %s."),
                    research_advance_name_translation(research, next_tech));
        notify_research(plr, E_TECH_LEARNED, ftc_server,
                        "%s%s", buffer1, buffer2);
      }
    }

    if (A_UNSET != next_tech) {
      choose_tech(plr, next_tech);
    } else {
      research->researching = A_UNSET;
      research->researching_cost = 0;
    }
  }

  if (!saving_bulbs && research->bulbs_researched > 0) {
    research->bulbs_researched = 0;
  }

  if (bonus_tech_hack) {
    if (advance_by_number(tech_found)->bonus_message) {
      notify_research(plr, E_TECH_GAIN, ftc_server,
                      "%s", _(advance_by_number(tech_found)->bonus_message));
    } else {
      notify_research(plr, E_TECH_GAIN, ftc_server,
                      _("Great scientists from all the "
                        "world join your civilization: you get "
                        "an immediate advance."));
    }

    give_immediate_free_tech(plr);
  }

  /*
   * Inform all players about new global advances to give them a
   * chance to obsolete buildings.
   */
  send_game_info(NULL);

  /*
   * Update all cities in case the tech changed some effects. This is
   * inefficient; it could be optimized if it's found to be a problem.  But
   * techs aren't researched that often.
   */
  cities_iterate(pcity) {
    /* Refresh the city data; this als updates the squared city radius. */
    city_refresh(pcity);
    send_city_info(city_owner(pcity), pcity);
  } cities_iterate_end;
  
  /*
   * Send all player an updated info of the owner of the Marco Polo
   * Wonder if this wonder has become obsolete.
   */
  players_iterate(owner) {
    if (had_embassies[player_index(owner)]  > 0
        && get_player_bonus(owner, EFT_HAVE_EMBASSIES) == 0) {
      players_iterate(other_player) {
        send_player_all_c(owner, other_player->connections);
      } players_iterate_end;
    }
  } players_iterate_end;

  packet.tech = tech_found;
  research_players_iterate(research, aplayer) {
    lsend_packet_tech_gained(aplayer->connections, &packet);
  } research_players_iterate_end;
}

/****************************************************************************
  Is player about to lose tech?
****************************************************************************/
static bool lose_tech(struct player *plr)
{
  struct research *research;

  if (game.server.techloss_forgiveness < 0) {
    /* Tech loss disabled */
    return FALSE;
  }

  research = research_get(plr);

  if (research->techs_researched == 0 && research->future_tech == 0) {
    /* No tech to lose */
    return FALSE;
  }

  if (research->bulbs_researched <
      -research->researching_cost * game.server.techloss_forgiveness / 100) {
    return TRUE;
  }

  return FALSE;
}

/****************************************************************************
  Adds the given number of bulbs into the player's tech and (if necessary and
  'check_tech' is TRUE) completes the research. If the total number of bulbs
  is negative due to tech upkeep, one (randomly chosen) tech is lost.

  The caller is responsible for sending updated player information.

  This is called from each city every turn, from caravan revenue, and at the
  end of the phase.
****************************************************************************/
bool update_bulbs(struct player *plr, int bulbs, bool check_tech)
{
  struct research *research = research_get(plr);

  /* count our research contribution this turn */
  plr->bulbs_last_turn += bulbs;
  research->bulbs_researched += bulbs;

  /* if we have a negative number of bulbs we do
   * - try to reduce the number of future techs
   * - or lose one random tech
   * after that the number of bulbs available is incresed based on the value
   * of the lost tech. */
  if (lose_tech(plr)) {
    Tech_type_id tech;

    if (research->future_tech > 0) {
      notify_player(plr, NULL, E_TECH_GAIN, ftc_server,
                    _("Insufficient science output. We lost Future Tech. %d."),
                    research->future_tech);
      log_debug("%s: tech loss (future tech %d)", player_name(plr),
                research->future_tech);

      tech = A_FUTURE;
      research->future_tech--;
    } else {
      tech = pick_random_tech_to_lose(plr);

      if (tech != A_NONE) {
        notify_player(plr, NULL, E_TECH_GAIN, ftc_server,
                      _("Insufficient science output. We lost %s."),
                      research_advance_name_translation(research, tech));
        log_debug("%s: tech loss (%s)", player_name(plr),
                  research_advance_rule_name(research, tech));

        player_tech_lost(plr, tech);
      }
    }

    if (tech != A_NONE) {
      if (game.server.techloss_restore >= 0) {
        research->bulbs_researched +=
            (research_total_bulbs_required(research, tech, TRUE)
             * game.server.techloss_restore / 100);
      } else {
        research->bulbs_researched = 0;
      }
    }

    research_update(research);
  }

  if (check_tech && research->researching != A_UNSET) {
    /* check for finished research */
    if (research->bulbs_researched - research->researching_cost >= 0) {
      tech_researched(plr);

      if (research->researching != A_UNSET) {
        /* check research again */
        update_bulbs(plr, 0, TRUE);
        return TRUE;
      }
    }
  }

  return FALSE;
}

/****************************************************************************
  Choose a random tech for player to lose.
****************************************************************************/
static Tech_type_id pick_random_tech_to_lose(struct player* plr)
{
  struct research *presearch = research_get(plr);
  bv_techs eligible_techs;
  int chosen, eligible = advance_count();

  BV_SET_ALL(eligible_techs);

  advance_index_iterate(A_FIRST, i) {
    if (research_invention_state(presearch, i) != TECH_KNOWN) {
      if (BV_ISSET(eligible_techs, i)) {
        eligible--;
        BV_CLR(eligible_techs, i);
      }
    } else {
      /* Knowing this tech may make others ineligible */
      Tech_type_id root = advance_required(i, AR_ROOT);
      /* Never lose techs that are root_req for a currently known tech
       * (including self root_req) */
      if (root != A_NONE && BV_ISSET(eligible_techs, root)) {
        eligible--;
        BV_CLR(eligible_techs, root);
      }
      if (!game.info.tech_loss_allow_holes) {
        /* Ruleset can prevent this kind of tech loss from opening up
         * holes in the tech tree */
        Tech_type_id prereq;
        prereq = advance_required(i, AR_ONE);
        if (prereq != A_NONE && BV_ISSET(eligible_techs, prereq)) {
          eligible--;
          BV_CLR(eligible_techs, prereq);
        }
        prereq = advance_required(i, AR_TWO);
        if (prereq != A_NONE && BV_ISSET(eligible_techs, prereq)) {
          eligible--;
          BV_CLR(eligible_techs, prereq);
        }
      }
    }
  } advance_index_iterate_end;

  if (eligible == 0) {
    /* no researched technology at all */
    return A_NONE;
  }

  chosen = fc_rand(eligible) + 1;

  advance_index_iterate(A_FIRST, i) {
    if (BV_ISSET(eligible_techs, i)) {
      chosen--;
      if (chosen == 0) {
        return i;
      }
    }
  } advance_index_iterate_end;

  /* should never be reached */
  return A_NONE;
}

/****************************************************************************
  Remove one tech from the player.
****************************************************************************/
static void player_tech_lost(struct player* plr, Tech_type_id tech)
{
  struct research *presearch = research_get(plr);
  bool old_gov[government_count()];

  if (tech == A_FUTURE) {
    presearch->future_tech--;
    research_update(presearch);
    return;
  }

  fc_assert_ret(valid_advance_by_number(tech));

  /* old available governments */
  fill_can_switch_to_government_array(plr, old_gov);

  /* remove technology */
  research_invention_set(presearch, tech, TECH_UNKNOWN);
  research_update(presearch);
  log_debug("%s lost tech id %d (%s)", player_name(plr), tech,
            advance_rule_name(advance_by_number(tech)));

  /* check governments */
  governments_iterate(gov) {
    if (government_of_player(plr) == gov
        && old_gov[government_index(gov)]
        && !can_change_to_government(plr, gov)) {
      /* Lost the technology for the government; switch to first
       * available government */
      bool new_gov_found = FALSE;
      governments_iterate(gov_new) {
        if (can_change_to_government(plr, gov_new)) {
          notify_player(plr, NULL, E_NEW_GOVERNMENT, ftc_server,
                        _("The required technology for our government '%s' "
                          "was lost. The citizens have started a "
                          "revolution into '%s'."),
                        government_name_translation(gov),
                        government_name_translation(gov_new));
          handle_player_change_government(plr, government_number(gov_new));
          new_gov_found = TRUE;
          break;
        }
      } governments_iterate_end;

      /* Do we have a government? */
      fc_assert_ret(new_gov_found);
      break;
    } else if (plr->target_government
               && plr->target_government == gov
               && !can_change_to_government(plr, gov)) {
      /* lost the technology for the target government; use the first
       * available government as new target government */
      bool new_gov_found = FALSE;
      governments_iterate(gov_new) {
        if (can_change_to_government(plr, gov_new)) {
          notify_player(plr, NULL, E_NEW_GOVERNMENT, ftc_server,
                        _("The required technology for our new government "
                          "'%s' was lost. The citizens chose '%s' as new "
                          "target government."),
                        government_name_translation(gov),
                        government_name_translation(gov_new));
          plr->target_government = gov_new;
          new_gov_found = TRUE;
          break;
        }
      } governments_iterate_end;

      /* Do we have a new traget government? */
      fc_assert_ret(new_gov_found);
      break;
    }
  } governments_iterate_end;

  /* check all settlers for valid activities */
  unit_list_iterate(plr->units, punit) {
    if (!can_unit_continue_current_activity(punit)) {
      log_debug("lost technology for activity of unit %s of %s (%d, %d)",
                unit_name_translation(punit), player_name(plr),
                TILE_XY(unit_tile(punit)));
      set_unit_activity(punit, ACTIVITY_IDLE);
    }
  } unit_list_iterate_end;

  /* check city production */
  city_list_iterate(plr->cities, pcity) {
    bool update = FALSE;

    if (pcity->production.kind == VUT_UTYPE
        && !can_city_build_unit_now(pcity, pcity->production.value.utype)) {
      notify_player(plr, pcity->tile, E_CITY_CANTBUILD, ftc_server,
                    _("%s can't build %s. The required technology was lost."),
                    city_name(pcity),
                    utype_name_translation(pcity->production.value.utype));
      choose_build_target(plr, pcity);

      update = TRUE;
    }

    if (pcity->production.kind == VUT_IMPROVEMENT
        && !can_city_build_improvement_now(pcity,
                                           pcity->production.value.building)) {
      notify_player(plr, pcity->tile, E_CITY_CANTBUILD, ftc_server,
                    _("%s can't build %s. The required technology was lost."),
                    city_name(pcity),
                    improvement_name_translation(pcity->production.value.building));
      choose_build_target(plr, pcity);

      update = TRUE;
    }

    if (advance_has_flag(tech, TF_POPULATION_POLLUTION_INC)) {
      update = TRUE;
    }

    if (update) {
      city_refresh(pcity);
      send_city_info(plr, pcity);
    }
  } city_list_iterate_end;
}

/****************************************************************************
  Returns random researchable tech or A_FUTURE.
  No side effects
****************************************************************************/
Tech_type_id pick_random_tech(struct player* plr) 
{
  const struct research *presearch = research_get(plr);
  int chosen, researchable = 0;

  advance_index_iterate(A_FIRST, i) {
    if (research_invention_state(presearch, i) == TECH_PREREQS_KNOWN) {
      researchable++;
    }
  } advance_index_iterate_end;
  if (researchable == 0) {
    return A_FUTURE;
  }
  chosen = fc_rand(researchable) + 1;
  
  advance_index_iterate(A_FIRST, i) {
    if (research_invention_state(presearch, i) == TECH_PREREQS_KNOWN) {
      chosen--;
      if (chosen == 0) {
        return i;
      }
    }
  } advance_index_iterate_end;
  log_error("Failed to pick a random tech.");
  return A_FUTURE;
}

/****************************************************************************
  Returns cheapest researchable tech, random among equal cost ones.
****************************************************************************/
Tech_type_id pick_cheapest_tech(struct player* plr)
{
  const struct research *presearch = research_get(plr);
  int cheapest_cost = -1;
  int cheapest_amount = 0;
  Tech_type_id cheapest = A_NONE;
  int chosen;

  advance_index_iterate(A_FIRST, i) {
    if (research_invention_state(presearch, i) == TECH_PREREQS_KNOWN) {
      int cost = research_total_bulbs_required(presearch, i, FALSE);

      if (cost < cheapest_cost || cheapest_cost == -1) {
        cheapest_cost = cost;
        cheapest_amount = 1;
        cheapest = i;
      } else if (cost == cheapest_cost) {
        cheapest_amount++;
      }
    }
  } advance_index_iterate_end;
  if (cheapest_cost == -1) {
    return A_FUTURE;
  }
  if (cheapest_amount == 1) {
    /* No need to get random one among the 1 cheapest */
    return cheapest;
  }

  chosen = fc_rand(cheapest_amount) + 1;

  advance_index_iterate(A_FIRST, i) {
    if (research_invention_state(presearch, i) == TECH_PREREQS_KNOWN
        && (research_total_bulbs_required(presearch, i, FALSE)
            == cheapest_cost)) {
      chosen--;
      if (chosen == 0) {
        return i;
      }
    }
  } advance_index_iterate_end;

  fc_assert(FALSE);

  return A_NONE;
}

/****************************************************************************
  Finds and chooses (sets) a random research target from among all those
  available until plr->research->researching != A_UNSET.
  Player may research more than one tech in this function.
  Possible reasons:
  - techpenalty < 100
  - research.got_tech = TRUE and enough bulbs was saved
  - research.researching = A_UNSET and enough bulbs was saved
****************************************************************************/
void choose_random_tech(struct player *plr)
{
  struct research* research = research_get(plr);
  do {
    choose_tech(plr, pick_random_tech(plr));
  } while (research->researching == A_UNSET);
}

/****************************************************************************
  Called when the player chooses the tech he wants to research (or when
  the server chooses it for him automatically).

  This takes care of all side effects so the player's research target
  probably shouldn't be changed outside of this function (doing so has
  been the cause of several bugs).
****************************************************************************/
void choose_tech(struct player *plr, Tech_type_id tech)
{
  struct research *research = research_get(plr);

  if (research->researching == tech) {
    return;
  }
  if (!is_future_tech(tech)
      && research_invention_state(research, tech) != TECH_PREREQS_KNOWN) {
    /* can't research this */
    return;
  }
  if (!research->got_tech && research->researching_saved == A_UNKNOWN) {
    research->bulbs_researching_saved = research->bulbs_researched;
    research->researching_saved = research->researching;
    /* subtract a penalty because we changed subject */
    if (research->bulbs_researched > 0) {
      research->bulbs_researched
        -= ((research->bulbs_researched * game.server.techpenalty) / 100);
      fc_assert(research->bulbs_researched >= 0);
    }
  } else if (tech == research->researching_saved) {
    research->bulbs_researched = research->bulbs_researching_saved;
    research->researching_saved = A_UNKNOWN;
  }
  research->researching=tech;
  research->researching_cost = research_total_bulbs_required(research, tech,
                                                             FALSE);
  if (research->bulbs_researched >= research->researching_cost) {
    tech_researched(plr);
  }
}

/****************************************************************************
  Called when the player chooses the tech goal he wants to research (or when
  the server chooses it for him automatically).
****************************************************************************/
void choose_tech_goal(struct player *plr, Tech_type_id tech)
{
  struct research *research = research_get(plr);

  if (research && tech != research->tech_goal) {
    /* It's been suggested that if the research target is empty then
     * choose_random_tech should be called here. */
    research->tech_goal = tech;
    notify_research(plr, E_TECH_GOAL, ftc_server,
                    _("Technology goal is %s."),
                    research_advance_name_translation(research, tech));
  }
}

/****************************************************************************
  Initializes tech data for the player.
****************************************************************************/
void init_tech(struct player *plr, bool update)
{
  struct research *research = research_get(plr);

  research_invention_set(research, A_NONE, TECH_KNOWN);

  advance_index_iterate(A_FIRST, i) {
    research_invention_set(research, i, TECH_UNKNOWN);
  } advance_index_iterate_end;

#ifdef TECH_UPKEEP_DEBUGGING
  /* Print a list of the needed upkeep if 'i' techs are researched.
   * If the ruleset contains self-rooted techs this can not work! */
  {
    bool global_state[A_LAST];
    Tech_type_id tech = A_LAST;

    /* Save the game research state. */
    advance_index_iterate(A_FIRST, i) {
      global_state[i] = game.info.global_advances[i];
    } advance_index_iterate_end;

    research->techs_researched = 1;
    research_update(presearch);

    /* Show research costs. */
    advance_index_iterate(A_NONE, i) {
      log_debug("[player %d] %-25s (ID: %3d) cost: %6d - reachable: %-3s "
                "(now) / %-3s (ever)", player_number(plr),
                advance_rule_name(advance_by_number(i)), i,
                research_total_bulbs_required(research, i, FALSE),
                research_invention_gettable(research, i, FALSE)
                ? "yes" : "no",
                research_invention_reachable(research, i) ? "yes" : "no");
    } advance_index_iterate_end;

    /* Update step for step each tech as known and print the upkeep. */
    while (tech != A_NONE) {
      tech = A_NONE;
      advance_index_iterate(A_FIRST, i) {
        if (research_invention_state(research, i) == TECH_PREREQS_KNOWN) {
          /* Found a tech which can be researched. */
          tech = i;
          break;
        }
      } advance_index_iterate_end;

      if (tech != A_NONE) {
        research->inventions[tech].state = TECH_KNOWN;
        research->techs_researched++;

        /* This will change the game state! */
        research_update(research);

        log_debug("[player %d] researched: %-25s (ID: %4d) techs: %3d "
                  "upkeep: %4d", player_number(plr),
                  advance_rule_name(advance_by_number(tech)), tech,
                  research->techs_researched, player_tech_upkeep(plr));
      }
    }

    /* Reset the changes done. */
    advance_index_iterate(A_FIRST, i) {
      research_invention_set(research, i, TECH_UNKNOWN);
      game.info.global_advances[i] = global_state[i];
    } advance_index_iterate_end;
  }
#endif /* TECH_UPKEEP_DEBUGGING */

  research->techs_researched = 1;

  if (update) {
    Tech_type_id next_tech;

    /* Mark the reachable techs */
    research_update(research);

    next_tech = research_goal_step(research, research->tech_goal);
    if (A_UNSET != next_tech) {
      choose_tech(plr, next_tech);
    } else {
      choose_random_tech(plr);
    }
  }
}

/****************************************************************************
  Gives global initial techs to the player.  The techs are read from the
  game ruleset file.
****************************************************************************/
void give_global_initial_techs(struct player *pplayer)
{
  struct research *presearch = research_get(pplayer);
  int i;

  for (i = 0; i < MAX_NUM_TECH_LIST; i++) {
    if (game.rgame.global_init_techs[i] == A_LAST) {
      break;
    }
    /* Maybe the player already got this tech by an other way (e.g. team). */
    if (research_invention_state(presearch, game.rgame.global_init_techs[i])
        != TECH_KNOWN) {
    found_new_tech(pplayer, game.rgame.global_init_techs[i],
                   FALSE, TRUE);
    }
  }
}

/****************************************************************************
  Gives nation specific initial techs to the player.  The techs are read
  from the nation ruleset file.
****************************************************************************/
void give_nation_initial_techs(struct player *pplayer)
{
  struct research *presearch = research_get(pplayer);
  const struct nation_type *pnation = nation_of_player(pplayer);
  int i;

  for (i = 0; i < MAX_NUM_TECH_LIST; i++) {
    if (pnation->init_techs[i] == A_LAST) {
      break;
    }
    /* Maybe the player already got this tech by an other way (e.g. team). */
    if (research_invention_state(presearch, pnation->init_techs[i])
        != TECH_KNOWN) {
      found_new_tech(pplayer, pnation->init_techs[i], FALSE, TRUE);
    }
  }
}

/****************************************************************************
  Gives a player random tech, which he hasn't researched yet.
  Returns the tech. This differs from give_random_free_tech - it doesn't
  apply free cost
****************************************************************************/
Tech_type_id give_random_initial_tech(struct player *pplayer)
{
  Tech_type_id tech;
  
  tech = pick_random_tech(pplayer);
  found_new_tech(pplayer, tech, FALSE, TRUE);
  return tech;
}

/****************************************************************************
  If victim has a tech which pplayer doesn't have, pplayer will get it.
  The clients will both be notified and the conquer cost
  penalty applied. Used for diplomats and city conquest.
  If preferred is A_UNSET one random tech will be chosen.
  Returns the stolen tech or A_NONE if no tech was found.
****************************************************************************/
Tech_type_id steal_a_tech(struct player *pplayer, struct player *victim,
                          Tech_type_id preferred)
{
  struct research *presearch, *vresearch;
  Tech_type_id stolen_tech = A_NONE;

  if (get_player_bonus(victim, EFT_NOT_TECH_SOURCE) > 0) {
    return A_NONE;
  }

  presearch = research_get(pplayer);
  vresearch = research_get(victim);

  if (preferred == A_UNSET) {
    int j = 0;
    advance_index_iterate(A_FIRST, i) {
      if (research_invention_gettable(presearch, i,
                                      game.info.tech_steal_allow_holes)
          && research_invention_state(presearch, i) != TECH_KNOWN
          && research_invention_state(vresearch, i) == TECH_KNOWN) {
        j++;
      }
    } advance_index_iterate_end;
  
    if (j == 0)  {
      /* we've moved on to future tech */
      if (vresearch->future_tech > presearch->future_tech) {
        found_new_tech(pplayer, A_FUTURE, FALSE, TRUE);	
        stolen_tech = A_FUTURE;
      } else {
        return A_NONE;
      }
    } else {
      /* pick random tech */
      j = fc_rand(j) + 1;
      stolen_tech = A_NONE; /* avoid compiler warning */
      advance_index_iterate(A_FIRST, i) {
        if (research_invention_gettable(presearch, i,
                                        game.info.tech_steal_allow_holes)
            && research_invention_state(presearch, i) != TECH_KNOWN
            && research_invention_state(vresearch, i) == TECH_KNOWN) {
	  j--;
        }
        if (j == 0) {
	  stolen_tech = i;
	  break;
        }
      } advance_index_iterate_end;
      fc_assert(stolen_tech != A_NONE);
    }
  } else { /* preferred != A_UNSET */
#ifndef NDEBUG
    if (!is_future_tech(preferred)) {
      fc_assert(NULL != valid_advance_by_number(preferred));
      fc_assert(TECH_KNOWN == research_invention_state(vresearch,
                                                       preferred));
    }
#endif
    stolen_tech = preferred;
  }

  notify_player(pplayer, NULL, E_MY_DIPLOMAT_THEFT, ftc_server,
                _("You steal %s from the %s."),
                research_advance_name_translation(presearch, stolen_tech),
                nation_plural_for_player(victim));

  notify_player(victim, NULL, E_ENEMY_DIPLOMAT_THEFT, ftc_server,
                _("The %s stole %s from you!"),
                nation_plural_for_player(pplayer),
                research_advance_name_translation(presearch, stolen_tech));

  notify_embassies(pplayer, victim, NULL, E_TECH_GAIN, ftc_server,
                   _("The %s have stolen %s from the %s."),
                   nation_plural_for_player(pplayer),
                   research_advance_name_translation(presearch, stolen_tech),
                   nation_plural_for_player(victim));

  if (tech_transfer(pplayer, victim, stolen_tech)) {
    do_conquer_cost(pplayer, stolen_tech);
    found_new_tech(pplayer, stolen_tech, FALSE, TRUE);

    script_server_signal_emit("tech_researched", 3,
                              API_TYPE_TECH_TYPE,
                              advance_by_number(stolen_tech),
                              API_TYPE_PLAYER, pplayer,
                              API_TYPE_STRING, "stolen");

    return stolen_tech;
  };

  return A_NONE;
}

/****************************************************************************
  Handle incoming research packet. Need to check correctness
  Set the player to be researching the given tech.

  If there are enough accumulated research points, the tech may be
  acquired immediately.
****************************************************************************/
void handle_player_research(struct player *pplayer, int tech)
{
  struct research *research = research_get(pplayer);

  if (tech != A_FUTURE && !valid_advance_by_number(tech)) {
    return;
  }
  
  if (tech != A_FUTURE
      && research_invention_state(research, tech) != TECH_PREREQS_KNOWN) {
    return;
  }

  choose_tech(pplayer, tech);

  /* Notify players sharing the same research. */
  send_research_info_to_owners(research);
}

/****************************************************************************
  Handle incoming player_tech_goal packet
  Called from the network or AI code to set the player's tech goal.
****************************************************************************/
void handle_player_tech_goal(struct player *pplayer, int tech_goal)
{
  struct research *research = research_get(pplayer);

  /* Set the tech goal to a defined state if it is
   * - not a future tech and not a valid goal
   * - not a future tech and not a valid advance
   * - not defined
   * - known (i.e. due to EFT_GIVE_IMM_TECH). */
  if ((tech_goal != A_FUTURE
       && (!valid_advance_by_number(tech_goal)
           || !research_invention_reachable(research, tech_goal)))
      || (tech_goal == A_NONE)
      || (TECH_KNOWN == research_invention_state(research, tech_goal))) {
    tech_goal = A_UNSET;
  }

  choose_tech_goal(pplayer, tech_goal);

  /* Notify players sharing the same research. */
  send_research_info_to_owners(research);
}

/****************************************************************************
  Gives a player random tech, which he hasn't researched yet. Applies freecost
  Returns the tech.
****************************************************************************/
Tech_type_id give_random_free_tech(struct player* pplayer)
{
  Tech_type_id tech;

  tech = pick_random_tech(pplayer);
  do_free_cost(pplayer, tech);
  found_new_tech(pplayer, tech, FALSE, TRUE);
  return tech;
}

/****************************************************************************
  Gives a player immediate free tech. Applies freecost
****************************************************************************/
Tech_type_id give_immediate_free_tech(struct player* pplayer)
{
  Tech_type_id tech;

  if (game.info.free_tech_method == FTM_CHEAPEST) {
    tech = pick_cheapest_tech(pplayer);
  } else if (research_get(pplayer)->researching == A_UNSET
      || game.info.free_tech_method == FTM_RANDOM) {
    return give_random_free_tech(pplayer);
  } else {
    tech = research_get(pplayer)->researching;
  }
  do_free_cost(pplayer, tech);
  found_new_tech(pplayer, tech, FALSE, TRUE);
  return tech;
}

/****************************************************************************
  Let the player forget one tech.
****************************************************************************/
static void forget_tech_transfered(struct player *pplayer, Tech_type_id tech)
{
  notify_player(pplayer, NULL, E_TECH_GAIN, ftc_server,
                _("Too bad! You made a mistake transferring the tech %s and "
                  "lost it."),
                research_advance_name_translation(research_get(pplayer),
                                                  tech));
  player_tech_lost(pplayer, tech);
  research_update(research_get(pplayer));
}

/****************************************************************************
  Check if the tech is lost by the donor or receiver. Returns if the
  receiver gets a new tech.
****************************************************************************/
bool tech_transfer(struct player *plr_recv, struct player *plr_donor,
                   Tech_type_id tech)
{
  if (game.server.techlost_donor > 0) {
    struct research *donor_research = research_get(plr_donor);
    bool donor_can_lose = TRUE;

    advance_index_iterate(A_FIRST, i) {
      /* Never let donor lose tech if it's root_req for some other known
       * tech */
      if (research_invention_state(donor_research, i) == TECH_KNOWN
          && (advance_required(i, AR_ROOT) == tech
              || (!game.info.tech_trade_loss_allow_holes
                  && (advance_required(i, AR_ONE) == tech
                      || advance_required(i, AR_TWO) == tech)))) {
        donor_can_lose = FALSE;
        break;
      }
    } advance_index_iterate_end;
    if (donor_can_lose && fc_rand(100) < game.server.techlost_donor) {
      forget_tech_transfered(plr_donor, tech);
    }
  }

  if (fc_rand(100) < game.server.techlost_recv) {
    forget_tech_transfered(plr_recv, tech);
    return FALSE;
  }

  return TRUE;
}
