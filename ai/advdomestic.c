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
#include "unittype.h"

#include "citytools.h"
#include "settlers.h"
#include "unittools.h"

#include "advmilitary.h"
#include "aicity.h"
#include "aidata.h"
#include "ailog.h"
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
				  struct ai_choice *choice)
{
  struct player *pplayer = city_owner(pcity);
  /* Continent where the city is --- we won't be aiding any wonder 
   * construction on another continent */
  Continent_id continent = map_get_continent(pcity->tile);
  /* Total count of caravans available or already being built 
   * on this continent */
  int caravans = 0;
  /* The type of the caravan */
  Unit_Type_id unit_type;

  if (num_role_units(F_HELP_WONDER) == 0) {
    /* No such units available in the ruleset */
    return;
  }

  /* Count existing caravans */
  unit_list_iterate(pplayer->units, punit) {
    if (unit_flag(punit, F_HELP_WONDER)
        && map_get_continent(punit->tile) == continent)
      caravans++;
  } unit_list_iterate_end;

  /* Count caravans being built */
  city_list_iterate(pplayer->cities, acity) {
    if (acity->is_building_unit
        && unit_type_flag(acity->currently_building, F_HELP_WONDER)
        && (acity->shield_stock
	    >= unit_build_shield_cost(acity->currently_building))
        && map_get_continent(acity->tile) == continent) {
      caravans++;
    }
  } city_list_iterate_end;

  /* Check all wonders in our cities being built, if one isn't worth a little
   * help */
  city_list_iterate(pplayer->cities, acity) {  
    unit_type = best_role_unit(pcity, F_HELP_WONDER);
    
    if (unit_type == U_LAST) {
      /* We cannot build such units yet
       * but we will consider it to stimulate science */
      unit_type = get_role_unit(F_HELP_WONDER, 0);
    }

    /* If we are building wonder there, the city is on same continent, we
     * aren't in that city (stopping building wonder in order to build caravan
     * to help it makes no sense) and we haven't already got enough caravans
     * to finish the wonder. */
    if (!acity->is_building_unit
        && is_wonder(acity->currently_building)
        && map_get_continent(acity->tile) == continent
        && acity != pcity
        && (build_points_left(acity)
	    > unit_build_shield_cost(unit_type) * caravans)) {
      
      /* Desire for the wonder we are going to help - as much as we want to
       * build it we want to help building it as well. */
      int want = pcity->ai.building_want[acity->currently_building];

      /* Distance to wonder city was established after ai_manage_buildings()
       * and before this.  If we just started building a wonder during
       * ai_city_choose_build(), the started_building notify comes equipped
       * with an update.  It calls generate_warmap(), but this is a lot less
       * warmap generation than there would be otherwise. -- Syela *
       * Value of 8 is a total guess and could be wrong, but it's still better
       * than 0. -- Syela */
      int dist = pcity->ai.distance_to_wonder_city * 8 / 
        get_unit_type(unit_type)->move_rate;

      want -= dist;
      
      if (can_build_unit_direct(pcity, unit_type)) {
        if (want > choice->want) {
          choice->want = want;
          choice->type = CT_NONMIL;
          ai_choose_role_unit(pplayer, pcity, choice, F_HELP_WONDER, dist / 2);
        }
      } else {
        int tech_req = get_unit_type(unit_type)->tech_requirement;

        /* XXX (FIXME): Had to add the scientist guess here too. -- Syela */
        pplayer->ai.tech_want[tech_req] += want;
      }
    }
  } city_list_iterate_end;
}

/************************************************************************** 
  This function should fill the supplied choice structure.

  If want is 0, this advisor doesn't want anything.
***************************************************************************/
void domestic_advisor_choose_build(struct player *pplayer, struct city *pcity,
				   struct ai_choice *choice)
{
  /* Government of the player */
  struct government *gov = get_gov_pplayer(pplayer);
  /* Unit type with certain role */
  Unit_Type_id unit_type;
  /* Food surplus assuming that workers and elvii are already accounted for
   * and properly balanced. */
  int est_food = pcity->food_surplus
                 + 2 * pcity->specialists[SP_SCIENTIST]
                 + 2 * pcity->specialists[SP_TAXMAN];

  init_choice(choice);

  /* Find out desire for settlers (terrain improvers) */
  unit_type = best_role_unit(pcity, F_SETTLERS);

  if (unit_type != U_LAST
      && est_food > utype_food_cost(get_unit_type(unit_type), gov)) {
    /* The settler want is calculated in settlers.c called from
     * ai_manage_cities.  The expand value is the % that the AI should
     * value expansion (basically to handicap easier difficutly levels)
     * and is set when the difficulty level is changed (stdinhand.c). */
    int want = pcity->ai.settler_want * pplayer->ai.expand / 100;

    /* Allowing multiple settlers per city now. I think this is correct.
     * -- Syela */

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

  if (unit_type != U_LAST
      && est_food >= utype_food_cost(get_unit_type(unit_type), gov)) {
    /* founder_want calculated in settlers.c, called from ai_manage_cities(). */
    int want = pcity->ai.founder_want;

    if (want > choice->want) {
      CITY_LOG(LOG_DEBUG, pcity, "desires founders with passion %d", want);
      choice->want = want;
      choice->need_boat = pcity->ai.founder_boat;
      choice->type = CT_NONMIL;
      ai_choose_role_unit(pplayer, pcity, choice, F_CITIES, want);
      
    } else if (want < -choice->want) {
      /* We need boats to colonize! */
      CITY_LOG(LOG_DEBUG, pcity, "desires founders with passion %d and asks"
	       " for a boat", want);
      choice->want = 0 - want;
      choice->type = CT_NONMIL;
      choice->choice = unit_type; /* default */
      choice->need_boat = TRUE;
      ai_choose_role_unit(pplayer, pcity, choice, L_FERRYBOAT, -want);
    }
  }

  {
    struct ai_choice cur;

    init_choice(&cur);
    /* Consider building caravan-type units to aid wonder construction */  
    ai_choose_help_wonder(pcity, &cur);
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
