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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "combat.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"
#include "unit.h"

#include "cityhand.h"
#include "citytools.h"
#include "cityturn.h"
#include "gotohand.h"
#include "plrhand.h"
#include "settlers.h"
#include "unithand.h"
#include "unittools.h"

#include "advdomestic.h"
#include "advmilitary.h"
#include "aidata.h"
#include "aihand.h"
#include "ailog.h"
#include "aitools.h"
#include "aiunit.h"

#include "aicity.h"

#define CITY_EMERGENCY(pcity)                        \
 (pcity->shield_surplus < 0 || city_unhappy(pcity)   \
  || pcity->food_stock + pcity->food_surplus < 0)
#define LOG_BUY LOG_DEBUG

static void resolve_city_emergency(struct player *pplayer, struct city *pcity);
static void ai_manage_city(struct player *pplayer, struct city *pcity);

/**************************************************************************
  This calculates the usefulness of pcity to us. Note that you can pass
  another player's ai_data structure here for evaluation by different
  priorities.
**************************************************************************/
int ai_eval_calc_city(struct city *pcity, struct ai_data *ai)
{
  int i = (pcity->food_surplus * ai->food_priority
           + pcity->shield_surplus * ai->shield_priority
           + pcity->luxury_total * ai->luxury_priority
           + pcity->tax_total * ai->gold_priority
           + pcity->science_total * ai->science_priority
           + pcity->ppl_happy[4] * ai->happy_priority
           - pcity->ppl_unhappy[4] * ai->unhappy_priority
           - pcity->ppl_angry[4] * ai->angry_priority
           - pcity->pollution * ai->pollution_priority);
  
  if (pcity->food_surplus < 0 || pcity->shield_surplus < 0) {
    /* The city is unmaintainable, it can't be good */
    i = MIN(i, 0);
  }

  return i;
}
     
/************************************************************************** 
...
**************************************************************************/
static void ai_manage_buildings(struct player *pplayer)
{ /* we have just managed all our cities but not chosen build for them yet */
  struct government *g = get_gov_pplayer(pplayer);
  Tech_Type_id j;
  int values[B_LAST], leon = 0;
  bool palace = FALSE;
  int corr = 0;
  memset(values, 0, sizeof(values));
  memset(pplayer->ai.tech_want, 0, sizeof(pplayer->ai.tech_want));

  if (find_palace(pplayer) || g->corruption_level == 0) palace = TRUE;
  city_list_iterate(pplayer->cities, pcity)
    ai_eval_buildings(pcity);
    if (!palace) corr += pcity->corruption * 8;
    impr_type_iterate(i) {
      if (pcity->ai.building_want[i] > 0) values[i] += pcity->ai.building_want[i];
    } impr_type_iterate_end;

    if (pcity->ai.building_want[B_LEONARDO] > leon)
      leon = pcity->ai.building_want[B_LEONARDO];
  city_list_iterate_end;

/* this is a weird place to iterate a units list! */
  unit_list_iterate(pplayer->units, punit)
    if (is_sailing_unit(punit))
      values[B_MAGELLAN] +=
	  unit_build_shield_cost(punit->type) * 2 * SINGLE_MOVE /
	  unit_type(punit)->move_rate;
  unit_list_iterate_end;
  values[B_MAGELLAN] *= 100 * SHIELD_WEIGHTING;
  values[B_MAGELLAN] /= (MORT * impr_build_shield_cost(B_MAGELLAN));

  /* This is a weird place to put tech advice */
  /* This was: > G_DESPOTISM; should maybe remove test, depending
   * on new government evaluation etc, but used for now for
   * regression testing --dwp */
  if (g->index != game.default_government
      && g->index != game.government_when_anarchy) {
    impr_type_iterate(i) {
      j = improvement_types[i].tech_req;
      if (get_invention(pplayer, j) != TECH_KNOWN)
        pplayer->ai.tech_want[j] += values[i];
      /* if it is a bonus tech double it's value since it give a free
	 tech */
      if (game.global_advances[j] == 0 && tech_flag(j, TF_BONUS_TECH))
	pplayer->ai.tech_want[j] *= 2;
      /* this probably isn't right -- Syela */
      /* since it assumes that the next tech is as valuable as the
	 current -- JJCogliati */
    } impr_type_iterate_end;
  } /* tired of researching pottery when we need to learn Republic!! -- Syela */


  city_list_iterate(pplayer->cities, pcity)
    pcity->ai.building_want[B_MAGELLAN] = values[B_MAGELLAN];
    pcity->ai.building_want[B_ASMITHS] = values[B_ASMITHS];
    pcity->ai.building_want[B_CURE] = values[B_CURE];
    pcity->ai.building_want[B_HANGING] += values[B_CURE];
    pcity->ai.building_want[B_WALL] = values[B_WALL];
    pcity->ai.building_want[B_HOOVER] = values[B_HOOVER];
    pcity->ai.building_want[B_BACH] = values[B_BACH];
/* yes, I know that HOOVER and BACH should be continent-only not global */
    pcity->ai.building_want[B_MICHELANGELO] = values[B_MICHELANGELO];
    pcity->ai.building_want[B_ORACLE] = values[B_ORACLE];
    pcity->ai.building_want[B_PYRAMIDS] = values[B_PYRAMIDS];
    pcity->ai.building_want[B_SETI] = values[B_SETI];
    pcity->ai.building_want[B_SUNTZU] = values[B_SUNTZU];
    pcity->ai.building_want[B_WOMENS] = values[B_WOMENS];
    pcity->ai.building_want[B_LEONARDO] = leon; /* hopefully will fix */
    pcity->ai.building_want[B_PALACE] = corr; /* urgent enough? */
  city_list_iterate_end;

  city_list_iterate(pplayer->cities, pcity) /* wonder-kluge */
    impr_type_iterate(i) {
      if (!pcity->is_building_unit && is_wonder(i) &&
          is_wonder(pcity->currently_building))
	/* this should encourage completion of wonders, I hope! -- Syela */
        pcity->ai.building_want[i] += pcity->shield_stock / 2;
    } impr_type_iterate_end;
  city_list_iterate_end;
}

/*************************************************************************** 
  This function computes distances between cities for purpose of building 
  crowds of water-consuming Caravans or smoggish Freights which want to add 
  their brick to the wonder being built in pcity.

  At the function entry point, our warmap is intact.  We need to do two 
  things: (1) establish "downtown" for THIS city (which is an estimate of 
  how much help we can expect when building a wonder) and 
  (2) establish distance to pcity for ALL cities on our continent.

  If there are more than one wondercity, things will get a bit random.
****************************************************************************/
static void establish_city_distances(struct player *pplayer, 
				     struct city *pcity)
{
  int distance, wonder_continent;
  Unit_Type_id freight = best_role_unit(pcity, F_HELP_WONDER);
  int moverate = (freight == U_LAST) ? SINGLE_MOVE
                                     : get_unit_type(freight)->move_rate;

  if (!pcity->is_building_unit && is_wonder(pcity->currently_building)) {
    wonder_continent = map_get_continent(pcity->x, pcity->y);
  } else {
    wonder_continent = 0;
  }

  pcity->ai.downtown = 0;
  city_list_iterate(pplayer->cities, othercity) {
    distance = WARMAP_COST(othercity->x, othercity->y);
    if (wonder_continent != 0
        && map_get_continent(othercity->x, othercity->y) == wonder_continent) {
      othercity->ai.distance_to_wonder_city = distance;
    }

    /* How many people near enough would help us? */
    distance += moverate - 1; distance /= moverate;
    pcity->ai.downtown += MAX(0, 5 - distance);
  } city_list_iterate_end;
}

/**************************************************************************
  Choose a build for the barbarian player.

  TODO: Move this into advmilitary.c
  TODO: It will be called for each city but doesn't depend on the city,
  maybe cache it?  Although barbarians don't normally have many cities, 
  so can be a bigger bother to cache it.
**************************************************************************/
static void ai_barbarian_choose_build(struct player *pplayer, 
				      struct ai_choice *choice)
{
  Unit_Type_id bestunit = -1;
  int i, bestattack = 0;

  /* Choose the best unit among the basic ones */
  for(i = 0; i < num_role_units(L_BARBARIAN_BUILD); i++) {
    Unit_Type_id iunit = get_role_unit(L_BARBARIAN_BUILD, i);

    if (get_unit_type(iunit)->attack_strength > bestattack) {
      bestunit = iunit;
      bestattack = get_unit_type(iunit)->attack_strength;
    }
  }

  /* Choose among those made available through other civ's research */
  for(i = 0; i < num_role_units(L_BARBARIAN_BUILD_TECH); i++) {
    Unit_Type_id iunit = get_role_unit(L_BARBARIAN_BUILD_TECH, i);

    if (game.global_advances[get_unit_type(iunit)->tech_requirement] != 0
	&& get_unit_type(iunit)->attack_strength > bestattack) {
      bestunit = iunit;
      bestattack = get_unit_type(iunit)->attack_strength;
    }
  }

  /* If found anything, put it into the choice */
  if (bestunit != -1) {
    choice->choice = bestunit;
    /* FIXME: 101 is the "overriding military emergency" indicator */
    choice->want   = 101;
    choice->type   = CT_ATTACKER;
  } else {
    freelog(LOG_VERBOSE, "Barbarians don't know what to build!");
  }
}

/************************************************************************** 
  Chooses what the city will build.  Is called after the military advisor
  put it's choice into pcity->ai.choice and "settler advisor" put settler
  want into pcity->founder_*.

  Note that AI cheats -- it suffers no penalty for switching from unit to 
  improvement, etc.
**************************************************************************/
static void ai_city_choose_build(struct player *pplayer, struct city *pcity)
{
  struct ai_choice newchoice;

  init_choice(&newchoice);

  if (ai_handicap(pplayer, H_AWAY)
      && game_next_year(pcity->turn_last_built) != game.year
      && pcity->ai.urgency == 0) {
    /* Don't change existing productions unless we have to. */
    return;
  }

  if( is_barbarian(pplayer) ) {
    ai_barbarian_choose_build(pplayer, &(pcity->ai.choice));
  } else {
    /* FIXME: 101 is the "overriding military emergency" indicator */
    if (pcity->ai.choice.want <= 100 || pcity->ai.urgency == 0) { 
      domestic_advisor_choose_build(pplayer, pcity, &newchoice);
      copy_if_better_choice(&newchoice, &(pcity->ai.choice));
    }
  }

  /* Fallbacks */
  if (pcity->ai.choice.want == 0) {
    /* Fallbacks do happen with techlevel 0, which is now default. -- Per */
    CITY_LOG(LOG_VERBOSE, pcity, "Falling back - didn't want to build soldiers,"
	     " settlers, or buildings");
    pcity->ai.choice.want = 1;
    if (best_role_unit(pcity, F_TRADE_ROUTE) != U_LAST) {
      pcity->ai.choice.choice = best_role_unit(pcity, F_TRADE_ROUTE);
      pcity->ai.choice.type = CT_NONMIL;
    } else if (can_build_improvement(pcity, B_CAPITAL)) {
      pcity->ai.choice.choice = B_CAPITAL;
      pcity->ai.choice.type = CT_BUILDING;
    } else if (best_role_unit(pcity, F_SETTLERS) != U_LAST) {
      pcity->ai.choice.choice = best_role_unit(pcity, F_SETTLERS);
      pcity->ai.choice.type = CT_NONMIL;
    } else {
      CITY_LOG(LOG_VERBOSE, pcity, "Cannot even build a fallback "
	       "(caravan/coinage/settlers). Fix the ruleset!");
      pcity->ai.choice.want = 0;
    }
  }

  if (pcity->ai.choice.want != 0) { 
    ASSERT_REAL_CHOICE_TYPE(pcity->ai.choice.type);

    CITY_LOG(LOG_DEBUG, pcity, "wants %s with desire %d.",
	     (is_unit_choice_type(pcity->ai.choice.type) ?
	      unit_name(pcity->ai.choice.choice) :
	      get_improvement_name(pcity->ai.choice.choice)),
	     pcity->ai.choice.want);
    
    if (!pcity->is_building_unit && is_wonder(pcity->currently_building) 
	&& (is_unit_choice_type(pcity->ai.choice.type) 
	    || pcity->ai.choice.choice != pcity->currently_building))
      notify_player_ex(NULL, pcity->x, pcity->y, E_WONDER_STOPPED,
		       _("Game: The %s have stopped building The %s in %s."),
		       get_nation_name_plural(pplayer->nation),
		       get_impr_name_ex(pcity, pcity->currently_building),
		       pcity->name);
    
    if (pcity->ai.choice.type == CT_BUILDING 
	&& is_wonder(pcity->ai.choice.choice)
	&& (pcity->is_building_unit 
	    || pcity->currently_building != pcity->ai.choice.choice)) {
      notify_player_ex(NULL, pcity->x, pcity->y, E_WONDER_STARTED,
		       _("Game: The %s have started building The %s in %s."),
		       get_nation_name_plural(city_owner(pcity)->nation),
		       get_impr_name_ex(pcity, pcity->ai.choice.choice),
		       pcity->name);
      pcity->currently_building = pcity->ai.choice.choice;
      pcity->is_building_unit = is_unit_choice_type(pcity->ai.choice.type);

      /* Help other cities to send caravans to us */
      generate_warmap(pcity, NULL);
      establish_city_distances(pplayer, pcity);
    } else {
      pcity->currently_building = pcity->ai.choice.choice;
      pcity->is_building_unit   = is_unit_choice_type(pcity->ai.choice.type);
    }
  }
}

/************************************************************************** 
...
**************************************************************************/
static void try_to_sell_stuff(struct player *pplayer, struct city *pcity)
{
  impr_type_iterate(id) {
    if (can_sell_building(pcity, id) && id != B_CITY) {
/* selling walls to buy defenders is counterproductive -- Syela */
      really_handle_city_sell(pplayer, pcity, id);
      break;
    }
  } impr_type_iterate_end;
}

/************************************************************************** 
  Increase maxbuycost.  This variable indicates (via ai_gold_reserve) to 
  the tax selection code how much money do we need for buying stuff.
**************************************************************************/
static void increase_maxbuycost(struct player *pplayer, int new_value)
{
  pplayer->ai.maxbuycost = MAX(pplayer->ai.maxbuycost, new_value);
}

/************************************************************************** 
  Try to upgrade a city's units. limit is the last amount of gold we can
  end up with after the upgrade. military is if we want to upgrade non-
  military or military units.
**************************************************************************/
static void ai_upgrade_units(struct city *pcity, int limit, bool military)
{
  struct player *pplayer = city_owner(pcity);
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit) {
    int id = can_upgrade_unittype(pplayer, punit->type);
    if (military && (!is_military_unit(punit) || !is_ground_unit(punit))) {
      /* Only upgrade military units this round */
      continue;
    } else if (!military && is_military_unit(punit)
               && unit_type(punit)->transport_capacity == 0) {
      /* Only civilians or tranports this round */
      continue;
    }
    if (id >= 0) {
      int cost = unit_upgrade_price(pplayer, punit->type, id);
      int real_limit = limit;
      /* Triremes are DANGEROUS!! We'll do anything to upgrade 'em. */
      if (unit_flag(punit, F_TRIREME)) {
        real_limit = pplayer->ai.est_upkeep;
      }
      if (pplayer->economic.gold - cost > real_limit) {
        CITY_LOG(LOG_BUY, pcity, "Upgraded %s to %s for %d (%s)",
                 unit_type(punit)->name, unit_types[id].name, cost,
                 military ? "military" : "civilian");
        handle_unit_upgrade(city_owner(pcity), punit->id);
      } else {
        increase_maxbuycost(pplayer, cost);
      }
    }
  } unit_list_iterate_end;
}

/************************************************************************** 
  Buy and upgrade stuff!
**************************************************************************/
static void ai_spend_gold(struct player *pplayer)
{
  struct ai_choice bestchoice;
  int cached_limit = ai_gold_reserve(pplayer);

  /* Disband explorers that are at home but don't serve a purpose. 
   * FIXME: This is a hack, and should be removed once we
   * learn how to ferry explorers to new land. */
  city_list_iterate(pplayer->cities, pcity) {
    struct tile *ptile = map_get_tile(pcity->x, pcity->y);
    unit_list_iterate_safe(ptile->units, punit) {
      if (unit_has_role(punit->type, L_EXPLORER)
          && pcity->id == punit->homecity
          && pcity->ai.urgency == 0) {
        CITY_LOG(LOG_BUY, pcity, "disbanding %s to increase production",
                 unit_name(punit->type));
	handle_unit_disband(pplayer,punit->id);
      }
    } unit_list_iterate_safe_end;
  } city_list_iterate_end;
  
  do {
    int limit = cached_limit; /* cached_limit is our gold reserve */
    struct city *pcity = NULL;
    bool expensive; /* don't buy when it costs x2 unless we must */
    int buycost;

    /* Find highest wanted item on the buy list */
    init_choice(&bestchoice);
    city_list_iterate(pplayer->cities, acity) {
      if (acity->ai.choice.want > bestchoice.want && ai_fuzzy(pplayer, TRUE)) {
        bestchoice.choice = acity->ai.choice.choice;
        bestchoice.want = acity->ai.choice.want;
        bestchoice.type = acity->ai.choice.type;
        pcity = acity;
      }
    } city_list_iterate_end;

    /* We found nothing, so we're done */
    if (bestchoice.want == 0) {
      break;
    }

    /* Not dealing with this city a second time */
    pcity->ai.choice.want = 0;

    ASSERT_REAL_CHOICE_TYPE(bestchoice.type);

    /* Try upgrade units at danger location (high want is usually danger) */
    if (pcity->ai.urgency > 1) {
      if (bestchoice.type == CT_BUILDING && is_wonder(bestchoice.choice)) {
        CITY_LOG(LOG_BUY, pcity, "Wonder being built in dangerous position!");
      } else {
        /* If we have urgent want, spend more */
        int upgrade_limit = limit;
        if (pcity->ai.urgency > 1) {
          upgrade_limit = pplayer->ai.est_upkeep;
        }
        /* Upgrade only military units now */
        ai_upgrade_units(pcity, upgrade_limit, TRUE);
      }
    }

    if (pcity->anarchy != 0 && bestchoice.type != CT_BUILDING) {
      continue; /* Nothing we can do */
    }

    /* Cost to complete production */
    buycost = city_buy_cost(pcity);

    if (buycost <= 0) {
      continue; /* Already completed */
    }

    if (bestchoice.type != CT_BUILDING
        && unit_type_flag(bestchoice.choice, F_CITIES)) {
      if (!city_got_effect(pcity, B_GRANARY) 
          && pcity->size == 1
          && city_granary_size(pcity->size)
             > pcity->food_stock + pcity->food_surplus) {
        /* Don't buy settlers in size 1 cities unless we grow next turn */
        continue;
      } else if (city_list_size(&pplayer->cities) > 6) {
          /* Don't waste precious money buying settlers late game
           * since this raises taxes, and we want science. Adjust this
           * again when our tax algorithm is smarter. */
          continue;
      }
    } else {
      /* We are not a settler. Therefore we increase the cash need we
       * balance our buy desire with to keep cash at hand for emergencies 
       * and for upgrades */
      limit *= 2;
    }

    /* It costs x2 to buy something with no shields contributed */
    expensive = (pcity->shield_stock == 0)
                || (pplayer->economic.gold - buycost < limit);

    if (bestchoice.type == CT_ATTACKER
	&& buycost > unit_build_shield_cost(bestchoice.choice) * 2) {
       /* Too expensive for an offensive unit */
       continue;
    }

    /* FIXME: Here Syela wanted some code to check if
     * pcity was doomed, and we should therefore attempt
     * to sell everything in it of non-military value */

    if (pplayer->economic.gold - pplayer->ai.est_upkeep >= buycost
        && (!expensive 
            || (pcity->ai.grave_danger != 0 && assess_defense(pcity) == 0)
            || (bestchoice.want > 200 && pcity->ai.urgency > 1))) {
      /* Buy stuff */
      CITY_LOG(LOG_BUY, pcity, "Crash buy of %s for %d (want %d)",
               bestchoice.type != CT_BUILDING ? unit_name(bestchoice.choice)
               : get_improvement_name(bestchoice.choice), buycost,
               bestchoice.want);
      really_handle_city_buy(pplayer, pcity);
    } else if (pcity->ai.grave_danger != 0 
               && bestchoice.type == CT_DEFENDER
               && assess_defense(pcity) == 0) {
      /* We have no gold but MUST have a defender */
      CITY_LOG(LOG_BUY, pcity, "must have %s but can't afford it (%d < %d)!",
	       unit_name(bestchoice.choice), pplayer->economic.gold, buycost);
      try_to_sell_stuff(pplayer, pcity);
      if (pplayer->economic.gold - pplayer->ai.est_upkeep >= buycost) {
        CITY_LOG(LOG_BUY, pcity, "now we can afford it (sold something)");
        really_handle_city_buy(pplayer, pcity);
      }
      increase_maxbuycost(pplayer, buycost);
    }
  } while (TRUE);

  /* Civilian upgrades now */
  city_list_iterate(pplayer->cities, pcity) {
    ai_upgrade_units(pcity, cached_limit, FALSE);
  } city_list_iterate_end;

  freelog(LOG_BUY, "%s wants to keep %d in reserve (tax factor %d)", 
          pplayer->name, cached_limit, pplayer->ai.maxbuycost);
}

/**************************************************************************
 cities, build order and worker allocation stuff here..
**************************************************************************/
void ai_manage_cities(struct player *pplayer)
{
  int i;
  pplayer->ai.maxbuycost = 0;

  city_list_iterate(pplayer->cities, pcity)
    if (CITY_EMERGENCY(pcity)) {
      /* Fix critical shortages or unhappiness */
      resolve_city_emergency(pplayer, pcity);
    }
    ai_manage_city(pplayer, pcity);
  city_list_iterate_end;

  ai_manage_buildings(pplayer);

  city_list_iterate(pplayer->cities, pcity)
    military_advisor_choose_build(pplayer, pcity, &pcity->ai.choice);
/* note that m_a_c_b mungs the seamap, but we don't care */
    establish_city_distances(pplayer, pcity); /* in advmilitary for warmap */
/* e_c_d doesn't even look at the seamap */
/* determines downtown and distance_to_wondercity, which a_c_c_b will need */
    contemplate_terrain_improvements(pcity);
    contemplate_new_city(pcity); /* while we have the warmap handy */
/* seacost may have been munged if we found a boat, but if we found a boat
we don't rely on the seamap being current since we will recalculate. -- Syela */

  city_list_iterate_end;

  city_list_iterate(pplayer->cities, pcity)
    ai_city_choose_build(pplayer, pcity);
  city_list_iterate_end;

  ai_spend_gold(pplayer);

  /* use ai_gov_tech_hints: */
  for(i=0; i<MAX_NUM_TECH_LIST; i++) {
    struct ai_gov_tech_hint *hint = &ai_gov_tech_hints[i];

    if (hint->tech == A_LAST)
      break;
    if (get_invention(pplayer, hint->tech) != TECH_KNOWN) {
      pplayer->ai.tech_want[hint->tech] +=
	  city_list_size(&pplayer->cities) * (hint->turns_factor *
					      num_unknown_techs_for_goal
					      (pplayer,
					       hint->tech) +
					      hint->const_factor);
      if (hint->get_first)
	break;
    } else {
      if (hint->done)
	break;
    }
  }
}

/************************************************************************** 
...
**************************************************************************/
void ai_choose_ferryboat(struct player *pplayer, struct city *pcity, struct ai_choice *choice)
{
  ai_choose_role_unit(pplayer, pcity, choice, L_FERRYBOAT,  choice->want);
}

/************************************************************************** 
  ...
**************************************************************************/
Unit_Type_id ai_choose_defender_versus(struct city *pcity, Unit_Type_id v)
{
  Unit_Type_id bestid = 0; /* ??? Zero is legal value! (Settlers by default) */
  int j, m;
  int best= 0;

  simple_ai_unit_type_iterate(i) {
    m = unit_types[i].move_type;
    if (can_build_unit(pcity, i) && (m == LAND_MOVING || m == SEA_MOVING)) {
      j = get_virtual_defense_power(v, i, pcity->x, pcity->y, FALSE, FALSE);
      if (j > best || (j == best && unit_build_shield_cost(i) <=
                               unit_build_shield_cost(bestid))) {
        best = j;
        bestid = i;
      }
    }
  } simple_ai_unit_type_iterate_end;

  return bestid;
}

/**************************************************************************
... 
**************************************************************************/
static bool building_unwanted(struct player *plr, Impr_Type_id i)
{
  return (ai_wants_no_science(plr)
	  && (i == B_LIBRARY || i == B_UNIVERSITY || i == B_RESEARCH));
}

/**************************************************************************
...
**************************************************************************/
static void ai_sell_obsolete_buildings(struct city *pcity)
{
  struct player *pplayer = city_owner(pcity);

  built_impr_iterate(pcity, i) {
    if(!is_wonder(i) 
       && i != B_CITY /* selling city walls is really, really dumb -- Syela */
       && (wonder_replacement(pcity, i) || building_unwanted(city_owner(pcity), i))) {
      do_sell_building(pplayer, pcity, i);
      notify_player_ex(pplayer, pcity->x, pcity->y, E_IMP_SOLD,
		       _("Game: %s is selling %s (not needed) for %d."), 
		       pcity->name, get_improvement_name(i), 
		       impr_sell_gold(i));
      return; /* max 1 building each turn */
    }
  } built_impr_iterate_end;
}

/**************************************************************************
 cities, build order and worker allocation stuff here..
**************************************************************************/
static void ai_manage_city(struct player *pplayer, struct city *pcity)
{
  auto_arrange_workers(pcity);
  ai_sell_obsolete_buildings(pcity);
  sync_cities();
}

/**************************************************************************
  This function tries desperately to save a city from going under by
  revolt or starvation of food or resources. We do this by taking
  over resources held by nearby cities and disbanding units.

  TODO: Try to move units into friendly cities to reduce unhappiness
  instead of disbanding. Also rather starve city than keep it in
  revolt, as long as we don't lose settlers.

  TODO: Make function that tries to save units by moving them into
  cities that can upkeep them and change homecity rather than just
  disband. This means we'll have to move this function to beginning
  of AI turn sequence (before moving units).

  "I don't care how slow this is; it will very rarely be used." -- Syela

  Syela is wrong. It happens quite too often, mostly due to unhappiness.
  Also, most of the time we are unable to resolve the situation. 
**************************************************************************/
static void resolve_city_emergency(struct player *pplayer, struct city *pcity)
#define LOG_EMERGENCY LOG_DEBUG
{
  struct city_list minilist;

  freelog(LOG_EMERGENCY,
          "Emergency in %s (%s, angry%d, unhap%d food%d, prod%d)",
          pcity->name, city_unhappy(pcity) ? "unhappy" : "content",
          pcity->ppl_angry[4], pcity->ppl_unhappy[4],
          pcity->food_surplus, pcity->shield_surplus);

  city_list_init(&minilist);
  map_city_radius_iterate(pcity->x, pcity->y, x, y) {
    struct city *acity = map_get_tile(x,y)->worked;
    int city_map_x, city_map_y;
    bool is_valid;

    if (acity && acity != pcity && acity->owner == pcity->owner)  {
      if (same_pos(acity->x, acity->y, x, y)) {
        /* can't stop working city center */
        continue;
      }
      freelog(LOG_DEBUG, "%s taking over %s's square in (%d, %d)",
              pcity->name, acity->name, x, y);
      is_valid = map_to_city_map(&city_map_x, &city_map_y, acity, x, y);
      assert(is_valid);
      server_remove_worker_city(acity, city_map_x, city_map_y);
      acity->ppl_elvis++;
      if (!city_list_find_id(&minilist, acity->id)) {
	city_list_insert(&minilist, acity);
      }
    }
  } map_city_radius_iterate_end;
  auto_arrange_workers(pcity);

  if (!CITY_EMERGENCY(pcity)) {
    freelog(LOG_EMERGENCY, "Emergency in %s resolved", pcity->name);
    goto cleanup;
  }

  unit_list_iterate_safe(pcity->units_supported, punit) {
    if (city_unhappy(pcity)
        && punit->unhappiness != 0
        && punit->ai.passenger == 0) {
      UNIT_LOG(LOG_EMERGENCY, punit, "is causing unrest, disbanded");
      handle_unit_disband(pplayer, punit->id);
      city_refresh(pcity);
    }
  } unit_list_iterate_safe_end;

  if (CITY_EMERGENCY(pcity)) {
    freelog(LOG_EMERGENCY, "Emergency in %s remains unresolved", 
            pcity->name);
  } else {
    freelog(LOG_EMERGENCY, 
            "Emergency in %s resolved by disbanding unit(s)", pcity->name);
  }

  cleanup:
  city_list_iterate(minilist, acity) {
    /* otherwise food total and stuff was wrong. -- Syela */
    city_refresh(acity);
    auto_arrange_workers(pcity);
  } city_list_iterate_end;

  city_list_unlink_all(&minilist);

  sync_cities();
}
#undef LOG_EMERGENCY
