/********************************************************************** 
 Freeciv - Copyright (C) 2002 - The Freeciv Project
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
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>

#include "city.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "mem.h"
#include "unit.h"

#include "citytools.h"
#include "maphand.h"
#include "settlers.h"
#include "unittools.h"

#include "advmilitary.h"
#include "aicity.h"
#include "aihand.h"
#include "aitools.h"
#include "aiunit.h"

#include "aidata.h"

static struct ai_data aidata[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];

/**************************************************************************
  Make and cache lots of calculations needed for other functions, notably:
  ai_eval_defense_land, ai_eval_defense_nuclear, ai_eval_defense_sea and 
  ai_eval_defense_air.

  Note: We use map.num_continents here rather than pplayer->num_continents
  because we are omniscient and don't care about such trivialities as who
  can see what.

  FIXME: We should try to find the lowest common defence strength of our
  defending units, and ignore enemy units that are incapable of harming 
  us, instead of just checking attack strength > 1.
**************************************************************************/
void ai_data_turn_init(struct player *pplayer) 
{
  struct ai_data *ai = &aidata[pplayer->player_no];
  int i, nuke_units = num_role_units(F_NUCLEAR);
  bool danger_of_nukes = FALSE;
  bool can_build_antisea = can_player_build_improvement(pplayer, B_COASTAL);
  bool can_build_antiair =  can_player_build_improvement(pplayer, B_SAM);
  bool can_build_antinuke = can_player_build_improvement(pplayer, B_SDI);
  bool can_build_antimissile = can_player_build_improvement(pplayer, B_SDI);

  /* Threats */

  ai->num_continents = map.num_continents;
  ai->threats.continent = fc_calloc(ai->num_continents + 1, sizeof(bool));
  ai->threats.invasions = FALSE;
  ai->threats.air       = FALSE;
  ai->threats.nuclear   = 0; /* none */
  ai->threats.sea       = FALSE;

  players_iterate(aplayer) {
    if (!is_player_dangerous(pplayer, aplayer)) {
      continue;
    }

    /* The idea is that if there aren't any hostile cities on
     * our continent, the danger of land attacks is not big
     * enough to warrant city walls. Concentrate instead on 
     * coastal fortresses and hunting down enemy transports. */
    city_list_iterate(aplayer->cities, acity) {
      int continent = map_get_continent(acity->x, acity->y);
      ai->threats.continent[continent] = TRUE;
    } city_list_iterate_end;

    unit_list_iterate(aplayer->units, punit) {
      if (is_sailing_unit(punit)) {
        /* If the enemy has not started sailing yet, or we have total
         * control over the seas, don't worry, keep attacking. */
        if (!ai->threats.invasions && is_ground_units_transport(punit)) {
          ai->threats.invasions = TRUE;
        }

        /* The idea is that while our enemies don't have any offensive
         * seaborne units, we don't have to worry. Go on the offensive! */
        if (can_build_antisea && unit_type(punit)->attack_strength > 1) {
          ai->threats.sea = TRUE;
        }
      }

      /* The next idea is that if our enemies don't have any offensive
       * airborne units, we don't have to worry. Go on the offensive! */
      if (can_build_antiair && (is_air_unit(punit) || is_heli_unit(punit))
           && unit_type(punit)->attack_strength > 1) {
        ai->threats.air = TRUE;
      }

      /* If our enemy builds missiles, worry about missile defence. */
      if (can_build_antimissile && unit_flag(punit, F_MISSILE)
          && unit_type(punit)->attack_strength > 1) {
        ai->threats.missile = TRUE;
      }

      /* If he builds nukes, worry a lot. */
      if (can_build_antinuke && unit_flag(punit, F_NUCLEAR)) {
        danger_of_nukes = TRUE;
      }

      /* If all our darkest fears have come true, we're done here. It
       * can't possibly get any worse! This shorts very long loops. */
      if ((ai->threats.air || !can_build_antiair)
          && (ai->threats.missile || !can_build_antimissile)
          && (danger_of_nukes || !can_build_antinuke)
          && (ai->threats.sea || !can_build_antisea)
          && ai->threats.invasions) {
        break;
      }
    } unit_list_iterate_end;

    /* Check for nuke capability */
    for (i = 0; i < nuke_units; i++) {
      Unit_Type_id nuke = get_role_unit(F_NUCLEAR, i);
      if (can_player_build_unit_direct(aplayer, nuke)) { 
        ai->threats.nuclear = 1;
      }
    }
  } players_iterate_end;

  /* Increase from fear to terror if opponent actually has nukes */
  if (danger_of_nukes) ai->threats.nuclear++; /* sum of both fears */

  /* Exploration */

  ai->explore.land_done = TRUE;
  ai->explore.sea_done = TRUE;
  ai->explore.continent = fc_calloc(ai->num_continents + 1, sizeof(bool));
  whole_map_iterate(x, y) {
    struct tile *ptile = map_get_tile(x, y);
    int continent = (int)map_get_continent(x, y);

    if (is_ocean(ptile->terrain)) {
      if (ai->explore.sea_done && ai_handicap(pplayer, H_TARGETS) 
          && !map_get_known(x, y, pplayer)) {
        ai->explore.sea_done = FALSE; /* we're not done there */
      }
      /* skip rest, which is land only */
      continue;
    }
    if (ai->explore.continent[ptile->continent]) {
      /* we don't need more explaining, we got the point */
      continue;
    }
    if ((map_has_special(x, y, S_HUT) 
         && (!ai_handicap(pplayer, H_HUTS)
             || map_get_known(x, y, pplayer)))
        || (ptile->city && unit_list_size(&ptile->units) == 0
            && pplayers_at_war(pplayer, city_owner(ptile->city)))) {
      /* hut, empty city... what is the difference? :) */
      ai->explore.land_done = FALSE;
      ai->explore.continent[continent] = TRUE;
      continue;
    }
    if (ai_handicap(pplayer, H_TARGETS) && !map_get_known(x, y, pplayer)) {
      /* this AI must explore */
      ai->explore.land_done = FALSE;
      ai->explore.continent[continent] = TRUE;
    }
  } whole_map_iterate_end;

  /* Statistics */

  ai->stats.workers = fc_calloc(ai->num_continents + 1, sizeof(int));
  ai->stats.cities = fc_calloc(ai->num_continents + 1, sizeof(int));
  ai->stats.average_production = 0;
  city_list_iterate(pplayer->cities, pcity) {
    ai->stats.cities[(int)map_get_continent(pcity->x, pcity->y)]++;
    ai->stats.average_production += pcity->shield_surplus;
  } city_list_iterate_end;
  ai->stats.average_production /= MAX(1, city_list_size(&pplayer->cities));
  BV_CLR_ALL(ai->stats.diplomat_reservations);
  unit_list_iterate(pplayer->units, punit) {
    struct tile *ptile = map_get_tile(punit->x, punit->y);

    if (!is_ocean(ptile->terrain) && unit_flag(punit, F_SETTLERS)) {
      ai->stats.workers[(int)map_get_continent(punit->x, punit->y)]++;
    }
    if (unit_flag(punit, F_DIPLOMAT) && punit->ai.ai_role == AIUNIT_ATTACK) {
      /* Heading somewhere on a mission, reserve target. */
      struct city *pcity = map_get_city(punit->goto_dest_x,
                                        punit->goto_dest_y);
      if (pcity) {
        BV_SET(ai->stats.diplomat_reservations, pcity->id);
      }
    }
  } unit_list_iterate_end;

  /* Diplomacy */

  /* Question: What can we accept as the reputation of a player before
   * we start taking action to prevent us from being suckered?
   * Answer: Very little. */
  ai->diplomacy.acceptable_reputation =
           GAME_DEFAULT_REPUTATION -
           GAME_DEFAULT_REPUTATION / 4;

  /* Set per-player variables. We must set all players, since players 
   * can be created during a turn, and we don't want those to have 
   * invalid values. */
  for (i = 0; i < MAX_NUM_PLAYERS; i++) {
    struct player *aplayer = get_player(i);

    ai->diplomacy.player_intel[i].is_allied_with_enemy = FALSE;
    ai->diplomacy.player_intel[i].at_war_with_ally = FALSE;
    ai->diplomacy.player_intel[i].is_allied_with_ally = FALSE;

    players_iterate(check_pl) {
      if (check_pl == pplayer || check_pl == aplayer
          || !check_pl->is_alive) {
        continue;
      }
      if (pplayers_allied(aplayer, check_pl)
          && pplayers_at_war(pplayer, check_pl)) {
       ai->diplomacy.player_intel[i].is_allied_with_enemy = TRUE;
      }
      if (pplayers_allied(pplayer, check_pl)
          && pplayers_at_war(aplayer, check_pl)) {
        ai->diplomacy.player_intel[i].at_war_with_ally = TRUE;
      }
      if (pplayers_allied(aplayer, check_pl)
          && pplayers_allied(pplayer, check_pl)) {
        ai->diplomacy.player_intel[i].is_allied_with_ally = TRUE;
      }
    } players_iterate_end;
  }

  /* 
   * Priorities. NEVER set these to zero! Weight values are usually
   * multiplied by these values, so be careful with them. They are
   * used in city calculations, and food and shields should be slightly
   * bigger because we only look at surpluses there. WAGs.
   */
  ai->food_priority = FOOD_WEIGHTING;
  ai->shield_priority = SHIELD_WEIGHTING;
  ai->luxury_priority = 1;
  ai->science_priority = TRADE_WEIGHTING;
  ai->gold_priority = TRADE_WEIGHTING;
  ai->happy_priority = 1;
  ai->unhappy_priority = TRADE_WEIGHTING; /* danger */
  ai->angry_priority = TRADE_WEIGHTING * 3; /* grave danger */
  ai->pollution_priority = POLLUTION_WEIGHTING;

  /* Goals */
  ai_best_government(pplayer);
}

/**************************************************************************
  Clean up our mess.
**************************************************************************/
void ai_data_turn_done(struct player *pplayer)
{
  struct ai_data *ai = &aidata[pplayer->player_no];

  free(ai->explore.continent); ai->explore.continent = NULL;
  free(ai->threats.continent); ai->threats.continent = NULL;
  free(ai->stats.workers);     ai->stats.workers = NULL;
  free(ai->stats.cities);      ai->stats.cities = NULL;
}

/**************************************************************************
  Return a pointer to our data
**************************************************************************/
struct ai_data *ai_data_get(struct player *pplayer)
{
  struct ai_data *ai = &aidata[pplayer->player_no];

  if (ai->num_continents != map.num_continents) {
    /* we discovered more continents, recalculate! */
    ai_data_turn_done(pplayer);
    ai_data_turn_init(pplayer);
  }
  return ai;
}
