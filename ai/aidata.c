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

#include "aisupport.h"
#include "city.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "rand.h"
#include "unit.h"

#include "citytools.h"
#include "diplhand.h"
#include "maphand.h"
#include "settlers.h"
#include "unittools.h"

#include "advdiplomacy.h"
#include "advmilitary.h"
#include "aicity.h"
#include "aiferry.h"
#include "aihand.h"
#include "ailog.h"
#include "aitools.h"
#include "aiunit.h"

#include "aidata.h"

static struct ai_data aidata[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];

/**************************************************************************
  Make and cache lots of calculations needed for other functions.

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
  int ally_strength = -1;
  struct player *ally_strongest = NULL;

  /*** Threats ***/

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

  /*** Exploration ***/

  ai->explore.land_done = TRUE;
  ai->explore.sea_done = TRUE;
  ai->explore.continent = fc_calloc(ai->num_continents + 1, sizeof(bool));
  whole_map_iterate(x, y) {
    struct tile *ptile = map_get_tile(x, y);
    int continent = (int)map_get_continent(x, y);

    if (is_ocean(ptile->terrain)) {
      if (ai->explore.sea_done && ai_handicap(pplayer, H_TARGETS) 
          && !map_is_known(x, y, pplayer)) {
	/* We're not done there. */
        ai->explore.sea_done = FALSE;
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
             || map_is_known(x, y, pplayer)))
        || (ptile->city && unit_list_size(&ptile->units) == 0
            && pplayers_at_war(pplayer, city_owner(ptile->city)))) {
      /* hut, empty city... what is the difference? :) */
      ai->explore.land_done = FALSE;
      ai->explore.continent[continent] = TRUE;
      continue;
    }
    if (ai_handicap(pplayer, H_TARGETS) && !map_is_known(x, y, pplayer)) {
      /* this AI must explore */
      ai->explore.land_done = FALSE;
      ai->explore.continent[continent] = TRUE;
    }
  } whole_map_iterate_end;

  /*** Statistics ***/

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
      struct city *pcity = map_get_city(goto_dest_x(punit),
					goto_dest_y(punit));;
      if (pcity) {
        BV_SET(ai->stats.diplomat_reservations, pcity->id);
      }
    }
  } unit_list_iterate_end;
  aiferry_init_stats(pplayer);

  /*** Diplomacy ***/

  if (pplayer->ai.control && !is_barbarian(pplayer)) {
    ai_diplomacy_calculate(pplayer, ai);
  }

  /* Question: What can we accept as the reputation of a player before
   * we start taking action to prevent us from being suckered?
   * Answer: Very little. */
  ai->diplomacy.acceptable_reputation =
           GAME_DEFAULT_REPUTATION -
           GAME_DEFAULT_REPUTATION / 4;

  /* Set per-player variables. We must set all players, since players 
   * can be created during a turn, and we don't want those to have 
   * invalid values. */
  for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
    struct player *aplayer = get_player(i);

    ai->diplomacy.player_intel[i].is_allied_with_enemy = NULL;
    ai->diplomacy.player_intel[i].at_war_with_ally = NULL;
    ai->diplomacy.player_intel[i].is_allied_with_ally = NULL;

    /* Determine who is the leader of our alliance. That is,
     * whoever has the more cities. */
    if (pplayers_allied(pplayer, aplayer)
        && city_list_size(&aplayer->cities) > ally_strength) {
      ally_strength = city_list_size(&aplayer->cities);
      ally_strongest = aplayer;
    }

    players_iterate(check_pl) {
      if (check_pl == pplayer
          || check_pl == aplayer
          || !check_pl->is_alive) {
        continue;
      }
      if (pplayers_allied(aplayer, check_pl)
          && pplayer_get_diplstate(pplayer, check_pl)->type == DS_WAR) {
       ai->diplomacy.player_intel[i].is_allied_with_enemy = check_pl;
      }
      if (pplayers_allied(pplayer, check_pl)
          && pplayer_get_diplstate(aplayer, check_pl)->type == DS_WAR) {
        ai->diplomacy.player_intel[i].at_war_with_ally = check_pl;
      }
      if (pplayers_allied(aplayer, check_pl)
          && pplayers_allied(pplayer, check_pl)) {
        ai->diplomacy.player_intel[i].is_allied_with_ally = check_pl;
      }
    } players_iterate_end;
  }
  if (ally_strongest != ai->diplomacy.alliance_leader) {
    ai->diplomacy.alliance_leader = ally_strongest;
  }
  ai->diplomacy.spacerace_leader = player_leading_spacerace();

  /*** Priorities ***/

  /* NEVER set these to zero! Weight values are usually multiplied by 
   * these values, so be careful with them. They are used in city 
   * and government calculations, and food and shields should be 
   * slightly bigger because we only look at surpluses there. They
   * are all WAGs. */
  ai->food_priority = FOOD_WEIGHTING;
  ai->shield_priority = SHIELD_WEIGHTING;
  if (ai_wants_no_science(pplayer)) {
    ai->luxury_priority = TRADE_WEIGHTING;
    ai->science_priority = 1;
  } else {
    ai->luxury_priority = 1;
    ai->science_priority = TRADE_WEIGHTING;
  }
  ai->gold_priority = TRADE_WEIGHTING;
  ai->happy_priority = 1;
  ai->unhappy_priority = TRADE_WEIGHTING; /* danger */
  ai->angry_priority = TRADE_WEIGHTING * 3; /* grave danger */
  ai->pollution_priority = POLLUTION_WEIGHTING;

  ai_best_government(pplayer);

  /*** Interception engine ***/

  /* We are tracking a unit if punit->ai.cur_pos is not NULL. If we
   * are not tracking, start tracking by setting cur_pos. If we are, 
   * fill prev_pos with previous cur_pos. This way we get the 
   * necessary coordinates to calculate a probably trajectory. */
  players_iterate(aplayer) {
    if (!aplayer->is_alive || aplayer == pplayer) {
      continue;
    }
    unit_list_iterate(aplayer->units, punit) {
      if (!punit->ai.cur_pos) {
        /* Start tracking */
        punit->ai.cur_pos = &punit->ai.cur_struct;
        punit->ai.prev_pos = NULL;
      } else {
        punit->ai.prev_struct = punit->ai.cur_struct;
        punit->ai.prev_pos = &punit->ai.prev_struct;
      }
      punit->ai.cur_pos->x = punit->x;
      punit->ai.cur_pos->y = punit->y;
    } unit_list_iterate_end;
  } players_iterate_end;
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

/**************************************************************************
  Initialize with sane values.
**************************************************************************/
void ai_data_init(struct player *pplayer)
{
  struct ai_data *ai = &aidata[pplayer->player_no];
  int i;

  ai->govt_reeval = 0;
  ai->government_want = fc_calloc(game.government_count + 1, sizeof(int));

  ai->diplomacy.target = NULL;
  ai->diplomacy.strategy = WIN_OPEN;
  ai->diplomacy.timer = 0;
  ai->diplomacy.countdown = 0;
  ai->diplomacy.love_coeff = 5; /* 5% */
  ai->diplomacy.love_incr = 4;
  ai->diplomacy.req_love_for_peace = 8;
  ai->diplomacy.req_love_for_alliance = 16;
  ai->diplomacy.req_love_for_ceasefire = 0;
  ai->diplomacy.alliance_leader = pplayer;

  for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
    ai->diplomacy.player_intel[i].spam = i; /* pseudorandom */
    ai->diplomacy.player_intel[i].distance = 1;
    ai->diplomacy.player_intel[i].ally_patience = 0;
    pplayer->ai.love[i] = 1;
    ai->diplomacy.player_intel[i].asked_about_peace = 0;
    ai->diplomacy.player_intel[i].asked_about_alliance = 0;
    ai->diplomacy.player_intel[i].asked_about_ceasefire = 0;
    ai->diplomacy.player_intel[i].warned_about_space = 0;
  }
}

/**************************************************************************
  Deinitialize data
**************************************************************************/
void ai_data_done(struct player *pplayer)
{
  struct ai_data *ai = &aidata[pplayer->player_no];

  free(ai->government_want);
}
