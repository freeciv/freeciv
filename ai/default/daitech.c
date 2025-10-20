/***********************************************************************
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

#include <string.h>

/* utility */
#include "log.h"

/* common */
#include "game.h"
#include "government.h"
#include "player.h"
#include "research.h"
#include "tech.h"

/* server */
#include "plrhand.h"
#include "srv_log.h"
#include "techtools.h"

/* server/advisors */
#include "advdata.h"

/* ai/default */
#include "daicity.h"
#include "daidata.h"
#include "daieffects.h"
#include "dailog.h"
#include "daiplayer.h"
#include "daiunit.h"

#include "daitech.h"

struct ai_tech_choice {
  Tech_type_id choice;        /* The id of the most needed tech */
  adv_want want;              /* Want of the most needed tech */
  adv_want current_want;      /* Want of the tech which is currently researched
                               * or is our current goal */
};

/**********************************************************************//**
  Massage the numbers provided to us by ai.tech_want into unrecognizable
  pulp.

  TODO: Write a transparent formula.

  Notes:
  1. research_goal_unknown_techs() returns 0 for known techs, 1 if tech
     is immediately available etc.
  2. A tech is reachable means we can research it now; tech is available
     means it's on our tech tree (different nations can have different techs).
  3. ai.tech_want is usually upped by each city, so it is divided by number
     of cities here.
  4. A tech isn't a requirement of itself.
**************************************************************************/
static void dai_select_tech(struct ai_type *ait,
                            struct player *pplayer,
                            struct ai_tech_choice *choice,
                            struct ai_tech_choice *goal)
{
  struct research *presearch = research_get(pplayer);
  Tech_type_id newtech, newgoal;
  int num_cities_nonzero = MAX(1, city_list_size(pplayer->cities));
  int values[MAX(A_ARRAY_SIZE, A_UNSET + 1)];
  int goal_values[MAX(A_ARRAY_SIZE, A_UNSET + 1)];
  struct ai_plr *plr_data = def_ai_player_data(pplayer, ait);
  Tech_type_id ac = advance_count();

  memset(values, 0, sizeof(values));
  values[A_UNSET] = -1;
  values[A_NONE] = -1;
  goal_values[A_UNSET] = -1;
  goal_values[A_NONE] = -1;

  /* If we are researching future techs, then simply continue with that
   * when possible. */
  if (is_future_tech(presearch->researching)) {
    bool real_found = FALSE;

    advance_index_iterate_max(A_FIRST, i, ac) {
      if (research_invention_state(presearch, i) == TECH_PREREQS_KNOWN) {
        real_found = TRUE;
        break;
      }
    } advance_index_iterate_max_end;

    if (!real_found) {
      if (choice) {
        choice->choice = presearch->researching;
        choice->want = 1;
        choice->current_want = 1;
      }
      if (goal) {
        goal->choice = A_UNSET;
        goal->want = 1;
        goal->current_want = 1;
      }
      return;
    }
  }

  /* Fill in values for the techs: want of the tech
   * + average want of those we will discover en route */
  advance_index_iterate_max(A_FIRST, i, ac) {
    if (valid_advance_by_number(i)) {
      int steps = research_goal_unknown_techs(presearch, i);

      /* We only want it if we haven't got it (so AI is human after all) */
      if (steps > 0) {
        values[i] += plr_data->tech_want[i];
        advance_index_iterate_max(A_FIRST, k, ac) {
          if (research_goal_tech_req(presearch, i, k)) {
            values[k] += plr_data->tech_want[i] / steps;
          }
        } advance_index_iterate_max_end;
      }
    }
  } advance_index_iterate_max_end;

  /* Fill in the values for the tech goals */
  advance_index_iterate_max(A_FIRST, i, ac) {
    if (valid_advance_by_number(i)) {
      int steps = research_goal_unknown_techs(presearch, i);

      if (steps == 0) {
        /* Can't be set as a goal any more */
        goal_values[i] = -1;
        continue;
      }

      goal_values[i] = values[i];
      advance_index_iterate_max(A_FIRST, k, ac) {
        if (research_goal_tech_req(presearch, i, k)) {
          goal_values[i] += values[k];
        }
      } advance_index_iterate_max_end;

      /* This is the best I could do. It still sometimes does freaky stuff
       * like setting goal to Republic and learning Monarchy, but that's what
       * it's supposed to be doing; it just looks strange. -- Syela */
      goal_values[i] /= steps;
      if (steps < 6) {
        log_debug("%s: want = " ADV_WANT_PRINTF ", value = %d, goal_value = %d",
                  advance_rule_name(advance_by_number(i)),
                  plr_data->tech_want[i],
                  values[i], goal_values[i]);
      }
    } else {
      goal_values[i] = -1;
    }
  } advance_index_iterate_max_end;

  newtech = A_UNSET;
  newgoal = A_UNSET;
  advance_index_iterate_max(A_FIRST, i, ac) {
    if (valid_advance_by_number(i)) {
      if (values[i] > values[newtech]
          && research_invention_gettable(presearch, i, TRUE)) {
        newtech = i;
      }
      if (goal_values[i] > goal_values[newgoal]
          && research_invention_reachable(presearch, i)) {
        newgoal = i;
      }
    }
  } advance_index_iterate_max_end;

#ifdef REALLY_DEBUG_THIS
  advance_index_iterate_max(A_FIRST, id, ac) {
    if (values[id] > 0
        && research_invention_state(presearch, id) == TECH_PREREQS_KNOWN) {
      TECH_LOG(ait, LOG_DEBUG, pplayer, advance_by_number(id),
               "turn end want: %d", values[id]);
    }
  } advance_index_iterate_max_end;
#endif /* REALLY_DEBUG_THIS */

  if (choice) {
    choice->choice = newtech;
    choice->want = values[newtech] / num_cities_nonzero;
    choice->current_want = (values[presearch->researching]
                            / num_cities_nonzero);
  }

  if (goal) {
    goal->choice = newgoal;
    goal->want = goal_values[newgoal] / num_cities_nonzero;
    goal->current_want = (goal_values[presearch->tech_goal]
                          / num_cities_nonzero);
    log_debug("Goal->choice = %s, goal->want = " ADV_WANT_PRINTF ", goal_value = %d, "
              "num_cities_nonzero = %d",
              research_advance_rule_name(presearch, goal->choice),
              goal->want,
              goal_values[newgoal],
              num_cities_nonzero);
  }

  /* We can't have this, which will happen in the circumstance
   * where all ai.tech_wants are negative */
  if (choice && choice->choice == A_UNSET) {
    choice->choice = presearch->researching;
  }

  return;
}

/**********************************************************************//**
  Calculates want for some techs by actually adding the tech and
  measuring the effect.
**************************************************************************/
static adv_want dai_tech_base_want(struct ai_type *ait, struct player *pplayer,
                                   struct city *pcity, struct advance *padv)
{
  struct research *pres = research_get(pplayer);
  Tech_type_id tech = advance_number(padv);
  enum tech_state old_state = research_invention_state(pres, tech);
  struct adv_data *adv = adv_data_get(pplayer, NULL);
  adv_want orig_want = dai_city_want(pplayer, pcity, adv, NULL);
  adv_want final_want;
  bool world_knew = game.info.global_advances[tech];
  int world_count = game.info.global_advance_count;

  research_invention_set(pres, tech, TECH_KNOWN);

  final_want = dai_city_want(pplayer, pcity, adv, NULL);

  research_invention_set(pres, tech, old_state);
  game.info.global_advances[tech] = world_knew;
  game.info.global_advance_count = world_count;

  return final_want - orig_want;
}

/**********************************************************************//**
  Add effect values in to tech wants.
**************************************************************************/
static void dai_tech_effect_values(struct ai_type *ait, struct player *pplayer)
{
  /* TODO: Currently this duplicates code from daicity.c improvement effect
   *       evaluating almost verbose - refactor so that they can share code. */
  struct adv_data *adv = adv_data_get(pplayer, NULL);
  struct ai_plr *aip = def_ai_player_data(pplayer, ait);
  int turns = 9999; /* TODO: Set to correct value */
  int nplayers = normal_player_count();

  /* Remove team members from the equation */
  players_iterate(aplayer) {
    if (aplayer->team
        && aplayer->team == pplayer->team
        && aplayer != pplayer) {
      nplayers--;
    }
  } players_iterate_end;

  advance_iterate(padv) {
    if (research_invention_state(research_get(pplayer), advance_number(padv))
        != TECH_KNOWN) {
      struct universal source = { .kind = VUT_ADVANCE, .value.advance = padv };

      city_list_iterate(pplayer->cities, pcity) {
        adv_want v;
        adv_want tech_want;
        bool capital;
        const struct req_context context = {
          .player = pplayer,
          .city = pcity,
        };

        v = dai_tech_base_want(ait, pplayer, pcity, padv);
        capital = is_capital(pcity);

        effect_list_iterate(get_req_source_effects(&source), peffect) {
          bool present = TRUE;
          bool active = TRUE;

          requirement_vector_iterate(&peffect->reqs, preq) {
            /* Check if all the requirements for the currently evaluated effect
             * are met, except for having the tech that we are evaluating.
             * TODO: Consider requirements that could be met later. */
            if (VUT_ADVANCE == preq->source.kind
                && preq->source.value.advance == padv) {
              present = preq->present;
              continue;
            }
            if (!is_req_active(&context, NULL, preq, RPT_POSSIBLE)) {
              active = FALSE;
              break; /* presence doesn't matter for inactive effects. */

            }
          } requirement_vector_iterate_end;

          if (active) {
            adv_want v1;

            v1 = dai_effect_value(pplayer, adv, pcity, capital,
                                  turns, peffect, 1,
                                  nplayers);

            if (!present) {
              /* Tech removes the effect */
              v -= v1;
            } else {
              v += v1;
            }
          }
        } effect_list_iterate_end;

        /* Same conversion factor as in want_tech_for_improvement_effect() */
        tech_want = v * 14 / 8;

        aip->tech_want[advance_index(padv)] += tech_want;
      } city_list_iterate_end;
    }
  } advance_iterate_end;
}

/**********************************************************************//**
  Key AI research function. Disable if we are in a team with human team
  mates in a research pool.
**************************************************************************/
void dai_manage_tech(struct ai_type *ait, struct player *pplayer)
{
  struct ai_tech_choice choice, goal;
  struct research *research = research_get(pplayer);
  /* Penalty for switching research */
  /* FIXME: get real penalty with game.server.techpenalty and multiresearch */
  int penalty = research->bulbs_researched - research->free_bulbs;
  penalty = MAX(penalty, 0);

  /* Even when we let human to do the final decision, we keep our
   * wants correctly calculated. Add effect values in */
  dai_tech_effect_values(ait, pplayer);

  /* If there are humans in our team, they will choose the techs */
  research_players_iterate(research, aplayer) {
    if (!is_ai(aplayer)) {
      return;
    }
  } research_players_iterate_end;

  dai_select_tech(ait, pplayer, &choice, &goal);
  if (choice.choice != research->researching) {
    /* changing */
    if (choice.want - choice.current_want > penalty
        && (penalty + research->bulbs_researched
            <= research_total_bulbs_required(research, research->researching,
                                             FALSE))) {
      TECH_LOG(ait, LOG_DEBUG, pplayer, advance_by_number(choice.choice),
               "new research, was %s, penalty was %d",
               research_advance_rule_name(research, research->researching),
               penalty);
      choose_tech(research, choice.choice);
    }
  }

  /* crossing my fingers on this one! -- Syela (seems to have worked!) */
  /* It worked, in particular, because the value it sets (research->tech_goal)
   * is practically never used, see the comment for ai_next_tech_goal */
  if (goal.choice != research->tech_goal) {
    log_debug("%s change goal from %s (want=" ADV_WANT_PRINTF
              ") to %s (want=" ADV_WANT_PRINTF ")",
              player_name(pplayer),
              research_advance_rule_name(research, research->tech_goal),
              goal.current_want,
              research_advance_rule_name(research, goal.choice),
              goal.want);
    choose_tech_goal(research, goal.choice);
  }
}

/**********************************************************************//**
  Returns the best defense multiplier unit we can build, or NULL if none.
  Assigns tech wants for techs to get better units, but only for the
  cheapest to research.
**************************************************************************/
struct unit_type *dai_wants_defender_against(struct ai_type *ait,
                                             const struct civ_map *nmap,
                                             struct player *pplayer,
                                             struct city *pcity,
                                             const struct unit_type *att, int want)
{
  struct research *presearch = research_get(pplayer);
  int best_avl_def = 0;
  struct unit_type *best_avl = NULL;
  int best_cost = FC_INFINITY;
  struct advance *best_tech = A_NEVER;
  struct unit_type *best_unit = NULL;
  struct tile *ptile = city_tile(pcity);
  int def_values[U_LAST];
  int att_idx = utype_index(att);
  int defbonus = 100
    + get_unittype_bonus(pplayer, ptile, att, NULL, EFT_DEFEND_BONUS);

  unit_type_iterate(deftype) {
    int mp_pct = deftype->cache.defense_mp_bonuses_pct[att_idx] + 100;
    int scramble = deftype->cache.scramble_coeff[att_idx];
    int div_bonus_pct = 100 + combat_bonus_against(att->bonuses, deftype,
                                               CBONUS_DEFENSE_DIVIDER_PCT)
        + 100 * combat_bonus_against(att->bonuses, deftype,
                                     CBONUS_DEFENSE_DIVIDER);
    /* Either the unit uses city defense bonus, or scrambles with its own one */
    int def = deftype->defense_strength
        * (scramble ? scramble : defbonus * mp_pct) / div_bonus_pct;

    def_values[utype_index(deftype)] = def;

    if (can_city_build_unit_now(nmap, pcity, deftype)) {
      if (def > best_avl_def) {
        best_avl_def = def;
        best_avl = deftype;
      }
    }
  } unit_type_iterate_end;

  unit_type_iterate(deftype) {
    if (def_values[utype_index(deftype)] > best_avl_def
        && !can_city_build_unit_now(nmap, pcity, deftype)
        && can_city_build_unit_later(nmap, pcity, deftype)) {
      /* It would be better than current best. Consider researching tech */
      const struct impr_type *building;
      int cost = 0;
      struct advance *itech = A_NEVER;
      bool impossible_to_get = FALSE;

      unit_tech_reqs_iterate(deftype, padv) {
        if (research_invention_state(presearch,
                                     advance_number(padv)) != TECH_KNOWN) {
          /* See if we want to invent this. */
          int ucost = research_goal_bulbs_required(presearch,
                                                   advance_number(padv));

          if (cost == 0 || ucost < cost) {
            cost = ucost;
            itech = padv;
          }
        }
      } unit_tech_reqs_iterate_end;

      building = utype_needs_improvement(deftype, pcity);
      if (building != NULL
          && !can_player_build_improvement_direct(pplayer, building)) {
        const struct req_context context = {
          .player = pplayer,
          .city = pcity,
          .tile = city_tile(pcity),
          .unittype = deftype,
          .building = building,
        };

        requirement_vector_iterate(&building->reqs, preq) {
          if (!is_req_active(&context, NULL, preq, RPT_CERTAIN)) {

            if (VUT_ADVANCE == preq->source.kind && preq->present) {
              int iimprtech = advance_number(preq->source.value.advance);
              int imprcost = research_goal_bulbs_required(presearch,
                                                          iimprtech);

              if (imprcost < cost || cost == 0) {
                /* If we already have the primary tech (cost == 0),
                 * or the building's tech is cheaper,
                 * go for the building's required tech. */
                itech = preq->source.value.advance;
                cost = imprcost;
              }
            } else if (!dai_can_requirement_be_met_in_city(preq, pplayer,
                                                           pcity)) {
              impossible_to_get = TRUE;
            }
          }
        } requirement_vector_iterate_end;
      }

      if (cost < best_cost && !impossible_to_get && itech != A_NEVER
          && research_invention_reachable(presearch, advance_number(itech))) {
        best_tech = itech;
        best_cost = cost;
        best_unit = deftype;
      }
    }
  } unit_type_iterate_end;

  if (A_NEVER != best_tech) {
    struct ai_plr *plr_data = def_ai_player_data(pplayer, ait);

    /* Crank up chosen tech want */
    if (best_avl != NULL
        && def_values[utype_index(best_unit)] <= 1.5 * best_avl_def) {
      /* We already have almost as good unit suitable for defending against this attacker */
      want /= 2;
    }

    plr_data->tech_want[advance_index(best_tech)] += want;
    TECH_LOG(ait, LOG_DEBUG, pplayer, best_tech,
             "+ %d for %s by role",
             want,
             utype_rule_name(best_unit));
  }

  return best_avl;
}

/**********************************************************************//**
  Returns the best unit we can build, or NULL if none. "Best" here
  means last in the unit list as defined in the ruleset. Assigns tech
  wants for techs to get better units with given role, but only for the
  cheapest to research "next" unit up the "chain".
**************************************************************************/
struct unit_type *dai_wants_role_unit(struct ai_type *ait, struct player *pplayer,
                                      struct city *pcity, int role, int want)
{
  struct research *presearch = research_get(pplayer);
  int i, n;
  int best_cost = FC_INFINITY;
  struct advance *best_tech = A_NEVER;
  struct unit_type *best_unit = NULL;
  struct unit_type *build_unit = NULL;
  const struct civ_map *nmap = &(wld.map);

  n = num_role_units(role);
  for (i = n - 1; i >= 0; i--) {
    struct unit_type *iunit = get_role_unit(role, i);

    if (can_city_build_unit_now(nmap, pcity, iunit)) {
      build_unit = iunit;
      break;
    } else if (can_city_build_unit_later(nmap, pcity, iunit)) {
      const struct impr_type *building;
      struct advance *itech = A_NEVER;
      int cost = 0;

      unit_tech_reqs_iterate(iunit, padv) {
        if (research_invention_state(presearch,
                                     advance_number(padv)) != TECH_KNOWN) {
          /* See if we want to invent this. */
          int ucost = research_goal_bulbs_required(presearch,
                                                   advance_number(padv));

          if (cost == 0 || ucost < cost) {
            cost = ucost;
            itech = padv;
          }
        }
      } unit_tech_reqs_iterate_end;

      building = utype_needs_improvement(iunit, pcity);
      if (building != NULL
          && !can_player_build_improvement_direct(pplayer, building)) {
        requirement_vector_iterate(&building->reqs, preq) {
          if (VUT_ADVANCE == preq->source.kind && preq->present) {
            int iimprtech = advance_number(preq->source.value.advance);

            if (TECH_KNOWN != research_invention_state(presearch,
                                                       iimprtech)) {
              int imprcost = research_goal_bulbs_required(presearch,
                                                          iimprtech);

              if (imprcost < cost || cost == 0) {
                /* If we already have the primary tech (cost == 0),
                 * or the building's tech is cheaper,
                 * go for the building's required tech. */
                itech = preq->source.value.advance;
                cost = imprcost;
              }
            }
          }
        } requirement_vector_iterate_end;
      }

      if (cost < best_cost && itech != A_NEVER
          && research_invention_reachable(presearch, advance_number(itech))) {
        best_tech = itech;
        best_cost = cost;
        best_unit = iunit;
      }
    }
  }

  if (A_NEVER != best_tech) {
    struct ai_plr *plr_data = def_ai_player_data(pplayer, ait);

    /* Crank up chosen tech want */
    if (build_unit != NULL) {
      /* We already have a role unit of this kind */
      want /= 2;
    }
    plr_data->tech_want[advance_index(best_tech)] += want;
    TECH_LOG(ait, LOG_DEBUG, pplayer, best_tech,
             "+ %d for %s by role",
             want,
             utype_rule_name(best_unit));
  }

  return build_unit;
}

/**********************************************************************//**
  Zero player tech wants
**************************************************************************/
void dai_clear_tech_wants(struct ai_type *ait, struct player *pplayer)
{
  struct ai_plr *plr_data = def_ai_player_data(pplayer, ait);

  advance_index_iterate(A_FIRST, i) {
    plr_data->tech_want[i] = 0;
  } advance_index_iterate_end;
}
