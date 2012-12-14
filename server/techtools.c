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

static Tech_type_id pick_random_tech_researched(struct player* plr);
static Tech_type_id pick_random_tech(struct player* plr);
static void player_tech_lost(struct player* plr, Tech_type_id tech);
static void forget_tech_transfered(struct player *pplayer, Tech_type_id tech);

/**************************************************************************
  Reduce dipl cost bulbs from player.
**************************************************************************/
void do_dipl_cost(struct player *pplayer, Tech_type_id tech)
{
  struct player_research * research = player_research_get(pplayer);

  research->bulbs_researched
    -= (base_total_bulbs_required(pplayer, tech) * game.server.diplcost) / 100;
  research->researching_saved = A_UNKNOWN;
}

/**************************************************************************
  Reduce free cost bulbs from player.
**************************************************************************/
void do_free_cost(struct player *pplayer, Tech_type_id tech)
{
  struct player_research * research = player_research_get(pplayer);

  research->bulbs_researched
    -= (base_total_bulbs_required(pplayer, tech) * game.server.freecost) / 100;
  research->researching_saved = A_UNKNOWN;
}

/**************************************************************************
  Reduce conquer cost bulbs from player.
**************************************************************************/
void do_conquer_cost(struct player *pplayer, Tech_type_id tech)
{
  struct player_research * research = player_research_get(pplayer);  

  research->bulbs_researched
    -= (base_total_bulbs_required(pplayer, tech) * game.server.conquercost) / 100;
  research->researching_saved = A_UNKNOWN;
}

/****************************************************************************
  Player has researched a new technology
****************************************************************************/
static void tech_researched(struct player *plr)
{
  struct player_research *research = player_research_get(plr);
  /* plr will be notified when new tech is chosen */

  if (!is_future_tech(research->researching)) {
    notify_embassies(plr, NULL, NULL, E_TECH_GAIN, ftc_server,
                     _("The %s have researched %s."), 
                     nation_plural_for_player(plr),
                     advance_name_researching(plr));

  } else {
    notify_embassies(plr, NULL, NULL, E_TECH_GAIN, ftc_server,
                     _("The %s have researched Future Tech. %d."), 
                     nation_plural_for_player(plr),
                     research->future_tech);
  
  }

  /* Deduct tech cost */
  research->bulbs_researched = 
      MAX(research->bulbs_researched - total_bulbs_required(plr), 0);

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
      if (player_invention_reachable(pplayer, i, FALSE)
          && player_invention_state(pplayer, i) != TECH_KNOWN) {
        int num_research = 0;

        player_researches_iterate(presearch) {
          if (presearch->inventions[i].state == TECH_KNOWN) {
            num_research++;
          }
        } player_researches_iterate_end;
        if (num_research >= mod) {
          notify_player(pplayer, NULL, E_TECH_GAIN, ftc_server,
                        _("%s acquired from %s!"),
                        advance_name_for_player(pplayer, i),
                        buf);
          notify_embassies(pplayer, NULL, NULL, E_TECH_GAIN, ftc_server,
                           _("The %s have acquired %s from %s."),
                           nation_plural_for_player(pplayer),
                           advance_name_for_player(pplayer, i),
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
  Update all player specific stuff after the tech_found was discovered.
  could_switch_to_government holds information about which 
  government the player could switch to before the tech was reached
****************************************************************************/
static void update_player_after_tech_researched(struct player* plr,
                                         Tech_type_id tech_found,
					 bool was_discovery,
					 bool* could_switch_to_government)
{
  player_research_update(plr);

  remove_obsolete_buildings(plr);
  
  /* Give free rails in every city */
  if (tech_found != A_FUTURE
   && advance_has_flag(tech_found, TF_RAILROAD)) {
    upgrade_city_rails(plr, was_discovery);  
  }
  
  /* Enhance vision of units if a player-ranged effect has changed.  Note
   * that world-ranged effects will not be updated immediately. */
  unit_list_refresh_vision(plr->units);

  /* Notify a player about new governments available */
  governments_iterate(gov) {
    if (!could_switch_to_government[government_index(gov)]
        && can_change_to_government(plr, gov)) {
      notify_player(plr, NULL, E_NEW_GOVERNMENT, ftc_server,
                    _("Discovery of %s makes the government form %s"
                      " available. You may want to start a revolution."),
                    advance_name_for_player(plr, tech_found),
                    government_name_translation(gov));
    }
  } governments_iterate_end;

  /* Inform player about his new tech. */
  send_player_info_c(plr, plr->connections);
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
  on to upgrade_city_rails. Logging & notification is not done here as 
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
  bool can_switch[player_slot_count()][government_count()];
  struct player_research *research = player_research_get(plr);
  struct advance *vap = valid_advance_by_number(tech_found);

  /* HACK: A_FUTURE doesn't "exist" and is thus not "available".  This may
   * or may not be the correct thing to do.  For these sanity checks we
   * just special-case it. */
  fc_assert_ret(tech_found == A_FUTURE
                || (vap && player_invention_state(plr, tech_found)
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
      if (vap == pimprove->obsolete_by
          && is_great_wonder(pimprove)
          && (pcity = city_from_great_wonder(pimprove))) {
        notify_player(city_owner(pcity), NULL, E_WONDER_OBSOLETE, ftc_server,
                      _("Discovery of %s OBSOLETES %s in %s!"), 
                      advance_name_for_player(city_owner(pcity), tech_found),
                      improvement_name_translation(pimprove),
                      city_link(pcity));
      }
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
  players_iterate(aplayer) {
    if (!players_on_same_team(aplayer, plr)) {
      continue;
    }
    fill_can_switch_to_government_array(aplayer,
                                        can_switch[player_index(aplayer)]);
  } players_iterate_end;


  /* Mark the tech as known in the research struct and update
   * global_advances array */
  player_invention_set(plr, tech_found, TECH_KNOWN);

  /* Make proper changes for all players sharing the research */  
  players_iterate(aplayer) {
    if (!players_on_same_team(aplayer, plr)) {
      continue;
    }
    update_player_after_tech_researched(aplayer, tech_found, was_discovery,
                                        can_switch[player_index(aplayer)]);
  } players_iterate_end;

  if (tech_found == research->tech_goal) {
    research->tech_goal = A_UNSET;
  }

  if (tech_found == research->researching) {
    /* Try to pick new tech to research. */
    Tech_type_id next_tech = player_research_step(plr, research->tech_goal);

    /* As this function can be recursive, we need to print the messages
     * before really picking the new technology. */
    if (A_UNSET != next_tech) {
      notify_research(plr, E_TECH_LEARNED, ftc_server,
                      _("Learned %s. Our scientists focus on %s; "
                        "goal is %s."),
                      advance_name_for_player(plr, tech_found),
                      advance_name_for_player(plr, next_tech),
                      advance_name_for_player(plr, research->tech_goal));
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
                        advance_name_for_player(plr, tech_found));
      } else if (!is_future_tech(next_tech) || !is_future_tech(tech_found)) {
        notify_research(plr, E_TECH_LEARNED, ftc_server,
                        _("Learned %s. Scientists choose to research %s."),
                        advance_name_for_player(plr, tech_found),
                        advance_name_for_player(plr, next_tech));
      } else {
        char buffer1[300], buffer2[300];

        /* FIXME: Handle the translation in a single string. */
        fc_snprintf(buffer1, sizeof(buffer1), _("Learned %s. "),
                    advance_name_for_player(plr, tech_found));
        research->future_tech++;
        fc_snprintf(buffer2, sizeof(buffer2), _("Researching %s."),
                    advance_name_for_player(plr, next_tech));
        notify_research(plr, E_TECH_LEARNED, ftc_server,
                        "%s%s", buffer1, buffer2);
      }
    }

    if (A_UNSET != next_tech) {
      choose_tech(plr, next_tech);
    } else {
      research->researching = A_UNSET;
    }
  }

  if (!saving_bulbs && research->bulbs_researched > 0) {
    research->bulbs_researched = 0;
  }

  if (bonus_tech_hack) {
    int saved_bulbs;

    if (advance_by_number(tech_found)->bonus_message) {
      notify_research(plr, E_TECH_GAIN, ftc_server,
                      "%s", _(advance_by_number(tech_found)->bonus_message));
    } else {
      notify_research(plr, E_TECH_GAIN, ftc_server,
                      _("Great scientists from all the "
                        "world join your civilization: you get "
                        "an immediate advance."));
    }
    
    if (research->researching == A_UNSET) {
      choose_random_tech(plr);
    }
    do_free_cost(plr, research->researching);
    /* Save current number of bulbs so we can ignore changes made
     * by tech_researched(). This is supposed to be free tech, not
     * something having full cost. */
    saved_bulbs = research->bulbs_researched;
    tech_researched(plr);
    research->bulbs_researched = saved_bulbs;
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
    /* update squared city radius */
    city_map_update_radius_sq(pcity, TRUE);
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
  struct player_research *research = player_research_get(plr);

  /* count our research contribution this turn */
  plr->bulbs_last_turn += bulbs;
  research->bulbs_researched += bulbs;

  /* if we have a negative number of bulbs we do
   * - try to reduce the number of future techs
   * - or lose one random tech
   * after that the number of bulbs available is set to zero */
  if (game.info.tech_upkeep_style > 0 && research->bulbs_researched < 0
      && (research->techs_researched > 0 || research->future_tech > 0)) {
    if (research->future_tech > 0) {
      notify_player(plr, NULL, E_TECH_GAIN, ftc_server,
                    _("Insufficient science output. We lost Future Tech. %d."),
                    research->future_tech);
      log_debug("%s: tech loss (future tech %d)", player_name(plr),
                research->future_tech);
      research->future_tech--;
    } else {
      Tech_type_id tech = pick_random_tech_researched(plr);
      if (tech != A_NONE) {
        notify_player(plr, NULL, E_TECH_GAIN, ftc_server,
                      _("Insufficient science output. We lost %s."),
                      advance_name_for_player(plr, tech));
        log_debug("%s: tech loss (%s)", player_name(plr),
                  advance_name_for_player(plr, tech));
        player_tech_lost(plr, tech);
      }
    }
    player_research_update(plr);

    research->bulbs_researched = 0;
  }

  if (check_tech && research->researching != A_UNSET) {
    /* check for finished research */
    if (research->bulbs_researched - total_bulbs_required(plr) >= 0) {
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
  Returns a random researched tech.
****************************************************************************/
static Tech_type_id pick_random_tech_researched(struct player* plr)
{
  int chosen, researched = 0;

  advance_index_iterate(A_FIRST, i) {
    if (player_invention_state(plr, i) == TECH_KNOWN) {
      researched++;
    }
  } advance_index_iterate_end;

  if (researched == 0) {
    /* no technology at all */
    return A_NONE;
  }

  chosen = fc_rand(researched) + 1;

  advance_index_iterate(A_FIRST, i) {
    if (player_invention_state(plr, i) == TECH_KNOWN) {
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
  bool old_gov[government_count()];

  if (tech == A_FUTURE) {
    player_research_get(plr)->future_tech--;
    player_research_update(plr);

    return;
  }

  fc_assert_ret(valid_advance_by_number(tech));

  /* old available governments */
  fill_can_switch_to_government_array(plr, old_gov);

  /* remove technology */
  player_invention_set(plr, tech, TECH_UNKNOWN);
  player_research_update(plr);
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
  if (advance_has_flag(tech, TF_BRIDGE)
      || advance_has_flag(tech, TF_RAILROAD)
      || advance_has_flag(tech, TF_FARMLAND)) {
    unit_list_iterate(plr->units, punit) {
      if (!can_unit_continue_current_activity(punit)) {
        log_debug("lost technology for activity of unit %s of %s (%d, %d)",
                  unit_name_translation(punit), player_name(plr),
                  TILE_XY(unit_tile(punit)));
        set_unit_activity(punit, ACTIVITY_IDLE);
      }
    } unit_list_iterate_end;
  }

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
static Tech_type_id pick_random_tech(struct player* plr) 
{
  int chosen, researchable = 0;

  advance_index_iterate(A_FIRST, i) {
    if (player_invention_state(plr, i) == TECH_PREREQS_KNOWN) {
      researchable++;
    }
  } advance_index_iterate_end;
  if (researchable == 0) {
    return A_FUTURE;
  }
  chosen = fc_rand(researchable) + 1;
  
  advance_index_iterate(A_FIRST, i) {
    if (player_invention_state(plr, i) == TECH_PREREQS_KNOWN) {
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
  struct player_research* research = player_research_get(plr);
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
  struct player_research *research = player_research_get(plr);

  if (research->researching == tech) {
    return;
  }
  if (player_invention_state(plr, tech) != TECH_PREREQS_KNOWN) {
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
  if (research->bulbs_researched > total_bulbs_required(plr)) {
    tech_researched(plr);
  }
}

/****************************************************************************
  Called when the player chooses the tech goal he wants to research (or when
  the server chooses it for him automatically).
****************************************************************************/
void choose_tech_goal(struct player *plr, Tech_type_id tech)
{
  struct player_research *research = player_research_get(plr);

  if (research && tech != research->tech_goal) {
    /* It's been suggested that if the research target is empty then
     * choose_random_tech should be called here. */
    research->tech_goal = tech;
    notify_research(plr, E_TECH_GOAL, ftc_server,
                    _("Technology goal is %s."),
                    advance_name_for_player(plr, tech));
  }
}

/****************************************************************************
  Initializes tech data for the player.
****************************************************************************/
void init_tech(struct player *plr, bool update)
{
  struct player_research *research = player_research_get(plr);

  player_invention_set(plr, A_NONE, TECH_KNOWN);

  advance_index_iterate(A_FIRST, i) {
    player_invention_set(plr, i, TECH_UNKNOWN);
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
    player_research_update(plr);

    /* Show research costs. */
    advance_index_iterate(A_NONE, i) {
      log_debug("[player %d] %-25s (ID: %3d) cost: %6d - reachable: %-3s "
                "(now) / %-3s (ever)", player_number(plr),
                advance_rule_name(advance_by_number(i)), i,
                base_total_bulbs_required(plr, i),
                player_invention_reachable(plr, i, FALSE) ? "yes" : "no",
                player_invention_reachable(plr, i, TRUE) ? "yes" : "no");
    } advance_index_iterate_end;

    /* Update step for step each tech as known and print the upkeep. */
    while (tech != A_NONE) {
      tech = A_NONE;
      advance_index_iterate(A_FIRST, i) {
        if (player_invention_state(plr, i) == TECH_PREREQS_KNOWN) {
          /* Found a tech which can be researched. */
          tech = i;
          break;
        }
      } advance_index_iterate_end;

      if (tech != A_NONE) {
        research->inventions[tech].state = TECH_KNOWN;
        research->techs_researched++;

        /* This will change the game state! */
        player_research_update(plr);

        log_debug("[player %d] researched: %-25s (ID: %4d) techs: %3d "
                  "upkeep: %4d", player_number(plr),
                  advance_rule_name(advance_by_number(tech)), tech,
                  research->techs_researched, research->tech_upkeep);
      }
    }

    /* Reset the changes done. */
    advance_index_iterate(A_FIRST, i) {
      player_invention_set(plr, i, TECH_UNKNOWN);
      game.info.global_advances[i] = global_state[i];
    } advance_index_iterate_end;
  }
#endif /* TECH_UPKEEP_DEBUGGING */

  research->techs_researched = 1;

  if (update) {
    Tech_type_id next_tech;

    /* Mark the reachable techs */
    player_research_update(plr);

    next_tech = player_research_step(plr, research->tech_goal);
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
  int i;

  for (i = 0; i < MAX_NUM_TECH_LIST; i++) {
    if (game.rgame.global_init_techs[i] == A_LAST) {
      break;
    }
    /* Maybe the player already got this tech by an other way (e.g. team). */
    if (player_invention_state(pplayer,
                               game.rgame.global_init_techs[i])
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
  const struct nation_type *pnation = nation_of_player(pplayer);
  int i;

  for (i = 0; i < MAX_NUM_TECH_LIST; i++) {
    if (pnation->init_techs[i] == A_LAST) {
      break;
    }
    /* Maybe the player already got this tech by an other way (e.g. team). */
    if (player_invention_state(pplayer, pnation->init_techs[i])
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
  Tech_type_id stolen_tech = A_NONE;
  
  if (preferred == A_UNSET) {
    int j = 0;
    advance_index_iterate(A_FIRST, i) {
      if (player_invention_reachable(pplayer, i, FALSE)
	  && player_invention_state(pplayer, i) != TECH_KNOWN
	  && player_invention_state(victim, i) == TECH_KNOWN) {
        j++;
      }
    } advance_index_iterate_end;
  
    if (j == 0)  {
      /* we've moved on to future tech */
      if (player_research_get(victim)->future_tech
        > player_research_get(pplayer)->future_tech) {
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
        if (player_invention_reachable(pplayer, i, FALSE)
	    && player_invention_state(pplayer, i) != TECH_KNOWN
	    && player_invention_state(victim, i) == TECH_KNOWN) {
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
    fc_assert((preferred == A_FUTURE
               && (player_invention_state(victim, A_FUTURE)
                   == TECH_PREREQS_KNOWN))
              || (valid_advance_by_number(preferred)
                  && (player_invention_state(victim, preferred)
                      == TECH_KNOWN)));
    stolen_tech = preferred;
  }

  notify_player(pplayer, NULL, E_MY_DIPLOMAT_THEFT, ftc_server,
                _("You steal %s from the %s."),
                advance_name_for_player(pplayer, stolen_tech),
                nation_plural_for_player(victim));

  notify_player(victim, NULL, E_ENEMY_DIPLOMAT_THEFT, ftc_server,
                _("The %s stole %s from you!"),
                nation_plural_for_player(pplayer),
                advance_name_for_player(pplayer, stolen_tech));

  notify_embassies(pplayer, victim, NULL, E_TECH_GAIN, ftc_server,
                   _("The %s have stolen %s from the %s."),
                   nation_plural_for_player(pplayer),
                   advance_name_for_player(pplayer, stolen_tech),
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
  Handle incoming player_research packet. Need to check correctness
  Set the player to be researching the given tech.

  If there are enough accumulated research points, the tech may be
  acquired immediately.
****************************************************************************/
void handle_player_research(struct player *pplayer, int tech)
{
  if (tech != A_FUTURE && !valid_advance_by_number(tech)) {
    return;
  }
  
  if (tech != A_FUTURE
      && player_invention_state(pplayer, tech) != TECH_PREREQS_KNOWN) {
    return;
  }

  choose_tech(pplayer, tech);
  send_player_info_c(pplayer, pplayer->connections);

  /* Notify Team members. 
   * They share same research struct.
   */
  players_iterate_alive(aplayer) {
    if (pplayer != aplayer
        && player_diplstate_get(pplayer, aplayer)->type == DS_TEAM) {
      send_player_info_c(aplayer, aplayer->connections);
    }
  } players_iterate_alive_end;
}

/****************************************************************************
  Handle incoming player_tech_goal packet
  Called from the network or AI code to set the player's tech goal.
****************************************************************************/
void handle_player_tech_goal(struct player *pplayer, int tech_goal)
{
  /* Set the tech goal to a defined state if it is
   * - not a future tech and not a valid goal
   * - not a future tech and not a valid advance
   * - not defined
   * - known (i.e. due to EFT_GIVE_IMM_TECH). */
  if ((tech_goal != A_FUTURE
       && (!valid_advance_by_number(tech_goal)
           || !player_invention_reachable(pplayer, tech_goal, FALSE)))
      || (tech_goal == A_NONE)
      || (TECH_KNOWN == player_invention_state(pplayer, tech_goal))) {
    tech_goal = A_UNSET;
  }

  choose_tech_goal(pplayer, tech_goal);
  send_player_info_c(pplayer, pplayer->connections);

  /* Notify Team members */
  players_iterate_alive(aplayer) {
    if (pplayer != aplayer
        && player_diplstate_get(pplayer, aplayer)->type == DS_TEAM
        && player_research_get(aplayer)->tech_goal != tech_goal) {
      handle_player_tech_goal(aplayer, tech_goal);
    }
  } players_iterate_alive_end;
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
  if (player_research_get(pplayer)->researching == A_UNSET) {
    return give_random_free_tech(pplayer);
  }
  tech = player_research_get(pplayer)->researching;
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
                  "lost it."), advance_name_for_player(pplayer, tech));
  player_tech_lost(pplayer, tech);
  player_research_update(pplayer);
}

/****************************************************************************
  Check if the tech is lost by the donor or receiver. Returns if the
  receiver gets a new tech.
****************************************************************************/
bool tech_transfer(struct player *plr_recv, struct player *plr_donor,
                   Tech_type_id tech)
{
  if (fc_rand(100) < game.server.techlost_donor) {
    forget_tech_transfered(plr_donor, tech);
  }

  if (fc_rand(100) < game.server.techlost_recv) {
    forget_tech_transfered(plr_recv, tech);
    return FALSE;
  }

  return TRUE;
}
