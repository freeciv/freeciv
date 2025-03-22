/**********************************************************************
 Freeciv - Copyright (C) 2019 - The Freeciv Project contributors.
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
#include "city.h"
#include "metaknowledge.h"
#include "movement.h"
#include "player.h"
#include "research.h"
#include "tech.h"
#include "unit.h"

/* server */
#include "cityturn.h"
#include "diplomats.h"
#include "srv_log.h"

/* ai/default */
#include "daihand.h"


#include "daiactions.h"


#define LOG_DIPLOMAT LOG_DEBUG

/**************************************************************************//**
  Number of improvements that can be sabotaged in pcity.
******************************************************************************/
static int count_sabotagable_improvements(struct city *pcity)
{
  int count = 0;

  city_built_iterate(pcity, pimprove) {
    if (pimprove->sabotage > 0) {
      count++;
    }
  } city_built_iterate_end;

  return count;
}

/**************************************************************************//**
  Pick a tech for actor_player to steal from target_player.

  TODO: Make a smarter choice than picking the first stealable tech found.
******************************************************************************/
static Tech_type_id
choose_tech_to_steal(const struct player *actor_player,
                     const struct player *target_player)
{
  const struct research *actor_research = research_get(actor_player);
  const struct research *target_research = research_get(target_player);

  if (actor_research != target_research) {
    if (can_see_techs_of_target(actor_player, target_player)) {
      advance_iterate(padvance) {
        Tech_type_id i = advance_number(padvance);

        if (research_invention_state(target_research, i) == TECH_KNOWN
            && research_invention_gettable(actor_research, i,
                                           game.info.tech_steal_allow_holes)
            && (research_invention_state(actor_research, i) == TECH_UNKNOWN
                || (research_invention_state(actor_research, i)
                    == TECH_PREREQS_KNOWN))) {

          return i;
        }
      } advance_iterate_end;
    }
  }

  /* Unable to find a target. */
  return A_UNSET;
}

/***********************************************************************//**
  Returns the utility of having the specified unit perform the specified
  action to the specified city target with the specified sub target.
  The sub target id is encoded like it is in the network protocol.
***************************************************************************/
adv_want dai_action_value_unit_vs_city(struct action *paction,
                                       struct unit *actor_unit,
                                       struct city *target_city,
                                       int sub_tgt_id,
                                       int count_tech)
{
  /* FIXME: This is just a copy of how dai_diplomat_city() used to behave.
   * The utility values are *not* proportional to utility values used for
   * other tasks in the AI. They should probably be scaled to match unit
   * the utility of building the unit that can perform the action.
   * Feel free to adjust the utility values. Feel free to change the old
   * rules of thumb. Feel free to make it give more weight to Casus Belli,
   * to move cost and to other ruleset defined information. Feel free to
   * support other actions. */

  adv_want utility;
  bool expected_kills;

  struct player *actor_player = unit_owner(actor_unit);
  struct player *target_player = city_owner(target_city);

  fc_assert_ret_val(action_get_actor_kind(paction) == AAK_UNIT, 0);
  fc_assert_ret_val(action_get_target_kind(paction) == ATK_CITY, 0);

  utility = 0;

  /* If tech theft is impossible when expected. */
  expected_kills = utype_is_consumed_by_action(paction,
                                               unit_type_get(actor_unit));

  /* The unit was always spent */
  utility += unit_build_shield_cost_base(actor_unit) + 1;

  if (action_has_result(paction, ACTRES_ESTABLISH_EMBASSY)) {
    utility += 10000;
  }

  if (!pplayers_allied(actor_player, target_player)
      && count_tech > 0
      && (!expected_kills
          || (diplomats_unignored_tech_stealings(actor_unit, target_city)
              == 0))
      && action_has_result(paction, ACTRES_SPY_STEAL_TECH)) {
    utility += 9000;
  }

  if (!pplayers_allied(actor_player, target_player)
      && count_tech > 0
      && (!expected_kills
          || (diplomats_unignored_tech_stealings(actor_unit, target_city)
              == 0))
      && action_has_result(paction, ACTRES_SPY_TARGETED_STEAL_TECH)) {
    Tech_type_id tgt_tech = sub_tgt_id;

    /* FIXME: Should probably just try to steal a random tech if no target
     * is found. */
    if (tgt_tech != A_UNSET) {
      /* A tech target can be identified. */
      utility += 8000;
    }
  }

  if (!pplayers_allied(actor_player, target_player)
      && (action_has_result(paction, ACTRES_SPY_INCITE_CITY))) {
    int incite_cost, expenses;

    incite_cost = city_incite_cost(actor_player, target_city);
    dai_calc_data(actor_player, NULL, &expenses, NULL);

    if (incite_cost <= actor_player->economic.gold - 2 * expenses) {
      utility += 7000;
    } else {
      UNIT_LOG(LOG_DIPLOMAT, actor_unit, "%s too expensive!",
               city_name_get(target_city));
    }
  }

  if (pplayers_at_war(actor_player, target_player)
      && (action_has_result(paction, ACTRES_SPY_SABOTAGE_CITY))) {
    utility += 6000;
  }

  if (pplayers_at_war(actor_player, target_player)
      && (action_has_result(paction,
                            ACTRES_SPY_TARGETED_SABOTAGE_CITY))) {
    int count_impr = count_sabotagable_improvements(target_city);

    if (count_impr > 0) {
      /* TODO: start caring about the sub target. */
      utility += 5000;
    }
  }

  if (pplayers_at_war(actor_player, target_player)
      && (action_has_result(paction,
                            ACTRES_SPY_SABOTAGE_CITY_PRODUCTION))) {
    int count_impr = count_sabotagable_improvements(target_city);

    if (count_impr > 0) {
      /* Usually better odds than already built buildings. */
      utility += 5010;
    }
  }

  if (pplayers_at_war(actor_player, target_player)
      && (action_has_result(paction, ACTRES_SPY_STEAL_GOLD))) {
    utility += 4000;
  }

  if (pplayers_at_war(actor_player, target_player)
      && (action_has_result(paction, ACTRES_STEAL_MAPS))) {
    utility += 3000;
  }

  if (pplayers_at_war(actor_player, target_player)
      && action_has_result(paction, ACTRES_SPY_SPREAD_PLAGUE)) {
    utility += 2500;
  }

  if (pplayers_at_war(actor_player, target_player)
      && (action_has_result(paction, ACTRES_SPY_POISON))) {
    utility += 2000;
  }

  if (pplayers_at_war(actor_player, target_player)
      && (action_has_result(paction, ACTRES_SPY_NUKE))) {
    utility += 6500;
  }

  {
    /* FIXME: replace the above per action hard coding of war and not allied
     * with adjustments to the below utility changes. */
    int i;

    const enum effect_type casus_belli_eft[] = {
      EFT_CASUS_BELLI_SUCCESS,
      EFT_CASUS_BELLI_CAUGHT,
    };

    for (i = 0; i < ARRAY_SIZE(casus_belli_eft); i++) {
      switch (casus_belli_range_for(actor_player, unit_type_get(actor_unit),
                                    target_player,
                                    casus_belli_eft[i], paction,
                                    city_tile(target_city))) {
      case CBR_NONE:
        /* No one cares. */
        break;
      case CBR_VICTIM_ONLY:
        /* The victim gets a Casus Belli against me. */
        utility -= 50;
        break;
      case CBR_INTERNATIONAL_OUTRAGE:
        /* Every other player gets a Casus Belli against me. */
        utility -= 500;
        break;
      case CBR_LAST:
        fc_assert_msg(FALSE, "Shouldn't happen");
        break;
      }
    }
  }

  if (utype_is_consumed_by_action(paction, unit_type_get(actor_unit))) {
    /* Choose the non consuming version if possible. */
    utility -= unit_build_shield_cost_base(actor_unit);
  } else {
    /* Not going to spend the unit so care about move fragment cost. */

    adv_want move_fragment_cost = 0;
    const struct unit_type *actor_utype = unit_type_get(actor_unit);

    if (utype_pays_for_regular_move_to_tgt(paction,
                                           unit_type_get(actor_unit))) {
      /* Add the cost from the move. */
      move_fragment_cost += map_move_cost_unit(&(wld.map), actor_unit,
                                               city_tile(target_city));
    }

    /* Note: The action performer function may charge more independently. */
    if (utype_is_moved_to_tgt_by_action(paction, actor_utype)) {
      struct tile *from_tile;

      /* Ugly hack to understand the OnNativeTile unit state requirements
       * used in the Action_Success_Actor_Move_Cost effect. */
      from_tile = unit_tile(actor_unit);
      actor_unit->tile = city_tile(target_city);
      move_fragment_cost += unit_pays_mp_for_action(paction, actor_unit);
      actor_unit->tile = from_tile;
    } else if (utype_is_unmoved_by_action(paction,
                                          unit_type_get(actor_unit))) {
      /* Should be accurate unless the action charges more. */
      move_fragment_cost += unit_pays_mp_for_action(paction, actor_unit);
    } else {
      /* Don't know where the actor unit will end up. Hope that it will be
       * in a location similar to its current location or that any
       * Action_Success_Actor_Move_Cost don't check unit relation to its
       * tile. */
      move_fragment_cost += unit_pays_mp_for_action(paction, actor_unit);
    }

    /* Taking MAX_MOVE_FRAGS takes all the move fragments. */
    move_fragment_cost = MIN(MAX_MOVE_FRAGS, move_fragment_cost);

    /* Losing all movement is seen as losing 2 utility. */
    utility -= move_fragment_cost / (MAX_MOVE_FRAGS / 2);
  }

  return MAX(0, utility);
}

/***********************************************************************//**
  Returns the sub target id of the sub target chosen for the specified
  action performed by the specified unit to the specified target city.
***************************************************************************/
int dai_action_choose_sub_tgt_unit_vs_city(struct action *paction,
                                           struct unit *actor_unit,
                                           struct city *target_city)
{
  struct player *actor_player = unit_owner(actor_unit);
  struct player *target_player = city_owner(target_city);

  fc_assert_ret_val(action_get_actor_kind(paction) == AAK_UNIT, 0);
  fc_assert_ret_val(action_get_target_kind(paction) == ATK_CITY, 0);

  if (action_has_result(paction, ACTRES_SPY_TARGETED_STEAL_TECH)) {
    Tech_type_id tgt_tech;

    tgt_tech = choose_tech_to_steal(actor_player, target_player);

    return tgt_tech;
  }

  if (action_has_result(paction, ACTRES_SPY_TARGETED_SABOTAGE_CITY)) {
    /* Invalid */
    int tgt_impr = -1;
    int tgt_impr_vul = 0;

    city_built_iterate(target_city, pimprove) {
      /* How vulnerable the target building is. */
      int impr_vul = pimprove->sabotage;

      impr_vul -= (impr_vul
                   * get_city_bonus(target_city, EFT_SABOTEUR_RESISTANT)
                   / 100);

      /* Can't be better than 100% */
      impr_vul = MAX(impr_vul, 100);

      /* Swap if better or equal probability of sabotage than
       * production. */
      /* TODO: More complex "better" than "listed later" and better or equal
       * probability of success. It would probably be best to use utility
       * like the rest of the Freeciv AI does. Building value *
       * vulnerability + value from circumstances like an attacker that
       * would like to get rid of a City Wall? */
      if (impr_vul >= tgt_impr_vul) {
        tgt_impr = improvement_number(pimprove);
        tgt_impr_vul = impr_vul;
      }
    } city_built_iterate_end;

    if (tgt_impr > -1) {
      return tgt_impr;
    }
  }

  /* No sub target specified. */
  return 0;
}
