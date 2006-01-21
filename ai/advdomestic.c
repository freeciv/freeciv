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
#include <config.h>
#endif

#include <string.h>

#include "city.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "unit.h"
#include "unitlist.h"
#include "unittype.h"

#include "citytools.h"
#include "settlers.h"
#include "unittools.h"

#include "advmilitary.h"
#include "aicity.h"
#include "aidata.h"
#include "ailog.h"
#include "aitech.h"
#include "aitools.h"
#include "aiunit.h"

#include "advdomestic.h"


/***************************************************************************
 * Evaluate the need for units (like caravans) that aid wonder construction.
 * If another city is building wonder and needs help but pplayer is not
 * advanced enough to build caravans, the corresponding tech will be 
 * stimulated.
 ***************************************************************************/
static void ai_choose_help_wonder(struct city *pcity,
				  struct ai_choice *choice,
                                  struct ai_data *ai)
{
  struct player *pplayer = city_owner(pcity);
  Continent_id continent = tile_get_continent(pcity->tile);
  /* Total count of caravans available or already being built 
   * on this continent */
  int caravans = 0;
  /* The type of the caravan */
  struct unit_type *unit_type;
  struct city *wonder_city = find_city_by_id(ai->wonder_city);

  if (num_role_units(F_HELP_WONDER) == 0) {
    /* No such units available in the ruleset */
    return;
  }

  if (pcity == wonder_city 
      || wonder_city == NULL
      || pcity->ai.distance_to_wonder_city <= 0
      || wonder_city->production.is_unit
      || !is_wonder(wonder_city->production.value)) {
    /* A distance of zero indicates we are very far away, possibly
     * on another continent. */
    return;
  }

  /* Count existing caravans */
  unit_list_iterate(pplayer->units, punit) {
    if (unit_flag(punit, F_HELP_WONDER)
        && tile_get_continent(punit->tile) == continent)
      caravans++;
  } unit_list_iterate_end;

  /* Count caravans being built */
  city_list_iterate(pplayer->cities, acity) {
    if (acity->production.is_unit
        && unit_type_flag(get_unit_type(acity->production.value),
			  F_HELP_WONDER)
        && tile_get_continent(acity->tile) == continent) {
      caravans++;
    }
  } city_list_iterate_end;

  unit_type = best_role_unit(pcity, F_HELP_WONDER);

  if (!unit_type) {
    /* We cannot build such units yet
     * but we will consider it to stimulate science */
    unit_type = get_role_unit(F_HELP_WONDER, 0);
  }

  /* Check if wonder needs a little help. */
  if (build_points_left(wonder_city) 
      > unit_build_shield_cost(unit_type) * caravans) {
    Impr_type_id wonder = wonder_city->production.value;
    int want = wonder_city->ai.building_want[wonder];
    int dist = pcity->ai.distance_to_wonder_city /
               unit_type->move_rate;

    assert(!wonder_city->production.is_unit);

    want /= MAX(dist, 1);
    CITY_LOG(LOG_DEBUG, pcity, "want %s to help wonder in %s with %d", 
             unit_name(unit_type), wonder_city->name, want);
    if (want > choice->want) {
      /* This sets our tech want in cases where we cannot actually build
       * the unit. */
      unit_type = ai_wants_role_unit(pplayer, pcity, F_HELP_WONDER, want);
      if (can_build_unit(pcity, unit_type)) {
        choice->want = want;
        choice->type = CT_NONMIL;
        choice->choice = unit_type->index;
      } else {
        CITY_LOG(LOG_DEBUG, pcity, "would but could not build %s, bumped reqs",
                 unit_name(unit_type));
      }
    }
  }
}

/************************************************************************** 
  This function should fill the supplied choice structure.

  If want is 0, this advisor doesn't want anything.
***************************************************************************/
void domestic_advisor_choose_build(struct player *pplayer, struct city *pcity,
				   struct ai_choice *choice)
{
  struct ai_data *ai = ai_data_get(pplayer);
  /* Unit type with certain role */
  struct unit_type *unit_type;

  init_choice(choice);

  /* Find out desire for workers (terrain improvers) */
  unit_type = best_role_unit(pcity, F_SETTLERS);

  if (unit_type
      && (pcity->id != ai->wonder_city || unit_type->pop_cost == 0)
      && pcity->surplus[O_FOOD] > utype_upkeep_cost(unit_type,
                                                    pplayer, O_FOOD)) {
    /* The worker want is calculated in settlers.c called from
     * ai_manage_cities.  The expand value is the % that the AI should
     * value expansion (basically to handicap easier difficutly levels)
     * and is set when the difficulty level is changed (stdinhand.c). */
    int want = pcity->ai.settler_want * pplayer->ai.expand / 100;

    if (ai->wonder_city == pcity->id) {
      want /= 5;
    }

    if (want > 0) {
      CITY_LOG(LOG_DEBUG, pcity, "desires terrain improvers with passion %d", 
               want);
      choice->want = want;
      choice->type = CT_NONMIL;
      ai_choose_role_unit(pplayer, pcity, choice, F_SETTLERS, want);
    }
    /* Terrain improvers don't use boats (yet) */
  }

  /* Find out desire for city founders */
  /* Basically, copied from above and adjusted. -- jjm */
  unit_type = best_role_unit(pcity, F_CITIES);

  if (unit_type
      && (pcity->id != ai->wonder_city
          || unit_type->pop_cost == 0)
      && pcity->surplus[O_FOOD] >= utype_upkeep_cost(unit_type,
                                                     pplayer, O_FOOD)) {
    /* founder_want calculated in aisettlers.c */
    int want = pcity->ai.founder_want;

    if (ai->wonder_city == pcity->id) {
      want /= 5;
    }
    
    if (ai->max_num_cities <= city_list_size(pplayer->cities)) {
      want /= 100;
    }

    if (want > choice->want) {
      CITY_LOG(LOG_DEBUG, pcity, "desires founders with passion %d", want);
      choice->want = want;
      choice->need_boat = pcity->ai.founder_boat;
      choice->type = CT_NONMIL;
      ai_choose_role_unit(pplayer, pcity, choice, F_CITIES, want);
      
    } else if (want < -choice->want) {
      /* We need boats to colonize! */
      /* We might need boats even if there are boats free,
       * if they are blockaded or in inland seas. */
      struct ai_data *ai = ai_data_get(pplayer);

      CITY_LOG(LOG_DEBUG, pcity, "desires founders with passion %d and asks"
	       " for a new boat (%d of %d free)",
	       want, ai->stats.available_boats, ai->stats.boats);
      choice->want = 0 - want;
      choice->type = CT_NONMIL;
      choice->choice = unit_type->index; /* default */
      choice->need_boat = TRUE;
      ai_choose_role_unit(pplayer, pcity, choice, L_FERRYBOAT, -want);
    }
  }

  {
    struct ai_choice cur;

    init_choice(&cur);
    /* Consider building caravan-type units to aid wonder construction */  
    ai_choose_help_wonder(pcity, &cur, ai);
    copy_if_better_choice(&cur, choice);

    init_choice(&cur);
    /* Consider city improvments */
    ai_advisor_choose_building(pcity, &cur);
    copy_if_better_choice(&cur, choice);
  }

  if (choice->want >= 200) {
    /* If we don't do following, we buy caravans in city X when we should be
     * saving money to buy defenses for city Y. -- Syela */
    choice->want = 199;
  }

  return;
}
