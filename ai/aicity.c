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
#include "advisland.h"
#include "advleader.h"
#include "advmilitary.h"
#include "aihand.h"
#include "aitools.h"
#include "aidata.h"
#include "ailog.h"
#include "aiunit.h"

#include "aicity.h"

static void ai_manage_city(struct player *pplayer, struct city *pcity);
     
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
	  unit_type(punit)->build_cost * 2 * SINGLE_MOVE /
	  unit_type(punit)->move_rate;
  unit_list_iterate_end;
  values[B_MAGELLAN] *= 100 * SHIELD_WEIGHTING;
  values[B_MAGELLAN] /= (MORT * improvement_value(B_MAGELLAN));

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

/************************************************************************** 
... change the build order.
**************************************************************************/
static void ai_city_choose_build(struct player *pplayer, struct city *pcity)
{
  struct ai_choice bestchoice, curchoice;

  init_choice(&bestchoice);

  if( is_barbarian(pplayer) ) {    /* always build best attack unit */
    Unit_Type_id i, iunit, bestunit = -1;
    int bestattack = 0;
    for(i = 0; i < num_role_units(L_BARBARIAN_BUILD); i++) {
      iunit = get_role_unit(L_BARBARIAN_BUILD, i);
      if (get_unit_type(iunit)->attack_strength > bestattack) {
	bestunit = iunit;
	bestattack = get_unit_type(iunit)->attack_strength;
      }
    }
    for(i = 0; i < num_role_units(L_BARBARIAN_BUILD_TECH); i++) {
      iunit = get_role_unit(L_BARBARIAN_BUILD_TECH, i);
      if (game.global_advances[get_unit_type(iunit)->tech_requirement] != 0
	  && get_unit_type(iunit)->attack_strength > bestattack) {
	bestunit = iunit;
	bestattack = get_unit_type(iunit)->attack_strength;
      }
    }
    if (bestunit != -1) {
      bestchoice.choice = bestunit;
      bestchoice.want   = 101;
      bestchoice.type   = CT_ATTACKER;
    }
    else {
      freelog(LOG_VERBOSE, "Barbarians don't know what to build!\n");
    }
  }
  else {

/* obsolete code destroyed */
/*  military_advisor_choose_build(pplayer, pcity, &curchoice); */
/* this is now handled in manage_city thanks to our friend ->ai.choice */
/*  copy_if_better_choice(&curchoice, &bestchoice); */

    copy_if_better_choice(&pcity->ai.choice, &bestchoice);

    if (bestchoice.want <= 100 || pcity->ai.urgency == 0) { /* soldier at 101 cannot be denied */
      domestic_advisor_choose_build(pplayer, pcity, &curchoice);
      copy_if_better_choice(&curchoice, &bestchoice);
    }

    pcity->ai.choice.choice = bestchoice.choice; /* we want to spend gold later */
    pcity->ai.choice.want = bestchoice.want; /* so that we spend it in the right city */
    pcity->ai.choice.type = bestchoice.type; /* instead of the one atop the list */
  }

  if (bestchoice.want != 0) { /* Note - on fallbacks, will NOT get stopped building msg */

    ASSERT_REAL_CHOICE_TYPE(bestchoice.type);

    freelog(LOG_DEBUG, "%s wants %s with desire %d.", pcity->name,
		  (is_unit_choice_type(bestchoice.type) ?
                   unit_name(bestchoice.choice) :
		   get_improvement_name(bestchoice.choice)),
		  bestchoice.want);
    if(!pcity->is_building_unit && is_wonder(pcity->currently_building) &&
       (is_unit_choice_type(bestchoice.type) ||
        bestchoice.choice != pcity->currently_building))
      notify_player_ex(NULL, pcity->x, pcity->y, E_WONDER_STOPPED,
		       _("Game: The %s have stopped building The %s in %s."),
		       get_nation_name_plural(pplayer->nation),
		       get_impr_name_ex(pcity, pcity->currently_building),
		       pcity->name);

    if (bestchoice.type == CT_BUILDING && (pcity->is_building_unit ||
                 pcity->currently_building != bestchoice.choice) &&
                 is_wonder(bestchoice.choice)) {
      notify_player_ex(NULL, pcity->x, pcity->y, E_WONDER_STARTED,
		       _("Game: The %s have started building The %s in %s."),
		       get_nation_name_plural(city_owner(pcity)->nation),
		       get_impr_name_ex(pcity, bestchoice.choice),
		       pcity->name);
      pcity->currently_building = bestchoice.choice;
      pcity->is_building_unit    = is_unit_choice_type(bestchoice.type);

      /* need to establish distance to wondercity */
      generate_warmap(pcity, NULL);

      establish_city_distances(pplayer, pcity); /* for caravans in other cities */
    } else {
      pcity->currently_building = bestchoice.choice;
      pcity->is_building_unit   = is_unit_choice_type(bestchoice.type);
    }

    return;
  } /* AI cheats -- no penalty for switching from unit to improvement, etc. */

  /* fallbacks should never happen anymore, but probably
     could somehow. -- Syela */
  freelog(LOG_VERBOSE, "Falling back - %s didn't want soldiers, settlers,"
                       " or buildings", pcity->name);
  if (can_build_improvement(pcity, B_BARRACKS)) {
    pcity->currently_building = B_BARRACKS;
    pcity->is_building_unit = FALSE;
  } else {
    pcity->currently_building = best_role_unit(pcity, F_SETTLERS); /* yes, this could be truly bad */
    pcity->is_building_unit = TRUE;
  }
/* I think fallbacks only happen with techlevel 0, and even then are rare.
I haven't seen them, but I want to somewhat prepare for them anyway. -- Syela */  
}

/************************************************************************** 
...
**************************************************************************/
#ifdef GRAVEDANGERWORKS
static int ai_city_defender_value(struct city *pcity, Unit_Type_id a_type,
                                  Unit_Type_id d_type)
{
  return unit_vulnerability_virtual2(a_type, d_type, pcity->x,
				     pcity->y, FALSE,
				     do_make_unit_veteran(pcity, d_type),
				     FALSE, 0);
}
#endif

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

#define LOG_BUY LOG_DEBUG
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
        struct packet_unit_request packet;
        packet.unit_id = punit->id;
        packet.city_id = pcity->id;
        CITY_LOG(LOG_BUY, pcity, "Upgraded %s to %s for %d (%s)",
                 unit_type(punit)->name, unit_types[id].name, cost,
                 military ? "military" : "civilian");
        handle_unit_upgrade_request(city_owner(pcity), &packet);
      } else {
        pplayer->ai.maxbuycost = MAX(pplayer->ai.maxbuycost, cost);
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

  /* Disband troops that are at home but don't serve a purpose. */
  city_list_iterate(pplayer->cities, pcity) {
    struct tile *ptile = map_get_tile(pcity->x, pcity->y);
    unit_list_iterate(ptile->units, punit) {
      if (((unit_types[punit->type].shield_cost > 0
            && pcity->shield_prod == 0)
           || unit_has_role(punit->type, L_EXPLORER))
          && pcity->id == punit->homecity
          && pcity->ai.urgency == 0
          && is_ground_unit(punit)) {
        struct packet_unit_request packet;
        packet.unit_id = punit->id;
        CITY_LOG(LOG_BUY, pcity, "disbanding %s to increase production",
                 unit_name(punit->type));
        handle_unit_disband(pplayer, &packet);
      }
    } unit_list_iterate_end;
  } city_list_iterate_end;
  
  do {
    int limit = cached_limit; /* cached_limit is our gold reserve */
    struct city *pcity = NULL;
    bool expensive; /* don't buy when it costs x2 unless we must */
    int buycost;

    /* Find highest wanted item on the buy list */
    init_choice(&bestchoice);
    city_list_iterate(pplayer->cities, acity) {
      if (acity->anarchy != 0) continue;
      if (acity->ai.choice.want > bestchoice.want && ai_fuzzy(pplayer, TRUE)) {
        bestchoice.choice = acity->ai.choice.choice;
        bestchoice.want = acity->ai.choice.want;
        bestchoice.type = acity->ai.choice.type;
        pcity = acity;
      }
    } city_list_iterate_end;

    /* We found nothing, so we're done */
    if (bestchoice.want == 0) break;

    /* Not dealing with this city a second time */
    pcity->ai.choice.want = 0;

    ASSERT_REAL_CHOICE_TYPE(bestchoice.type);

    /* Try upgrade units at danger location (high want is usually danger) */
    if (pcity->ai.danger > 1) {
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
        /* Don't build settlers in size 1 cities unless we grow next turn */
        continue;
      } else {
        if (city_list_size(&pplayer->cities) <= 8) {
          /* Make AI get gold for settlers early game */
          pplayer->ai.maxbuycost = MAX(pplayer->ai.maxbuycost, buycost);
        } else if (city_list_size(&pplayer->cities) > 25) {
          /* Don't waste precious money buying settlers late game */
          continue;
        }
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
        && buycost > unit_types[bestchoice.choice].build_cost * 2) {
       /* Too expensive for an offensive unit */
       continue;
    }

    if (!expensive && bestchoice.type != CT_BUILDING
        && (unit_type_flag(bestchoice.choice, F_TRADE_ROUTE) 
            || unit_type_flag(bestchoice.choice, F_HELP_WONDER))
        && buycost < unit_types[bestchoice.choice].build_cost * 2) {
      /* We need more money for buying caravans. Increasing
         maxbuycost will increase taxes */
      pplayer->ai.maxbuycost = MAX(pplayer->ai.maxbuycost, buycost);
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
      if (buycost > pplayer->ai.maxbuycost) {
        /* Consequently we need to raise more money through taxes */
        pplayer->ai.maxbuycost = MAX(pplayer->ai.maxbuycost, buycost);
      }
    }
  } while (TRUE);

  /* Civilian upgrades now */
  city_list_iterate(pplayer->cities, pcity) {
    ai_upgrade_units(pcity, cached_limit, FALSE);
  } city_list_iterate_end;

  if (pplayer->economic.gold + cached_limit < pplayer->ai.maxbuycost) {
    /* We have too much gold! Don't raise taxes */
    pplayer->ai.maxbuycost = 0;
  }

  freelog(LOG_BUY, "%s wants to keep %d in reserve (tax factor %d)", 
          pplayer->name, cached_limit, pplayer->ai.maxbuycost);
}
#undef LOG_BUY

/**************************************************************************
 cities, build order and worker allocation stuff here..
**************************************************************************/
void ai_manage_cities(struct player *pplayer)
{
  int i;
  pplayer->ai.maxbuycost = 0;

  city_list_iterate(pplayer->cities, pcity)
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
  don't ask me why this is in aicity, I can't even remember -- Syela 
**************************************************************************/
static Unit_Type_id ai_choose_attacker(struct city *pcity,
                                       enum unit_move_type which)
{
  Unit_Type_id bestid = 0; /* ??? Zero is legal value! (Settlers by default) */
  int best = 0;
  int cur;

  simple_ai_unit_type_iterate(i) {
    cur = ai_unit_attack_desirability(i);
    if (which == unit_types[i].move_type) {
      if (can_build_unit(pcity, i) && (cur > best || (cur == best &&
 get_unit_type(i)->build_cost <= get_unit_type(bestid)->build_cost))) {
        best = cur;
        bestid = i;
      }
    }
  } simple_ai_unit_type_iterate_end;

  return bestid;
}

/************************************************************************** 
  ...
**************************************************************************/
Unit_Type_id ai_choose_attacker_ground(struct city *pcity)
{
  return(ai_choose_attacker(pcity, LAND_MOVING));
}

/************************************************************************** 
  ...
**************************************************************************/
Unit_Type_id ai_choose_attacker_sailing(struct city *pcity)
{
  return(ai_choose_attacker(pcity, SEA_MOVING));
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
      if (j > best || (j == best && get_unit_type(i)->build_cost <=
                               get_unit_type(bestid)->build_cost)) {
        best = j;
        bestid = i;
      }
    }
  } simple_ai_unit_type_iterate_end;

  return bestid;
}

/************************************************************************** 
   I'm not sure how to generalize this for rulesets, because I
   don't understand what its trying to return.  A military ground
   unit with hp>10 could be a Cannon, which doesn't seem like
   a "normal defender" to me...   But I think I have to do something,
   because for the Civ1 ruleset, all units have hp==10, so this
   would return 0.  Hence this strange a+2*d formula, which gives
   the same results as (hp>10) for the default set, and does
   _something_ for other sets.  This should be revisited.  --dwp
**************************************************************************/
bool has_a_normal_defender(struct city *pcity)
{
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    if (is_military_unit(punit)
	&& is_ground_unit(punit)
	&& (unit_type(punit)->attack_strength
	    + 2*unit_type(punit)->defense_strength) >= 9
	&& can_build_unit(pcity, punit->type)) {
      return TRUE;
    }
  unit_list_iterate_end;
  return FALSE;
}

/************************************************************************** 
  ...
**************************************************************************/
Unit_Type_id ai_choose_defender_limited(struct city *pcity, int n,
                                        enum unit_move_type which)
{
  Unit_Type_id bestid = 0; /* ??? Zero is legal value! (Settlers by default) */
  int j, m;
  int best= 0;
  const bool walls = TRUE; /* just assume city_got_citywalls(pcity); in the long run -- Syela */
  bool isdef = has_a_normal_defender(pcity);

  simple_ai_unit_type_iterate(i) {
    m = unit_types[i].move_type;
    if (can_build_unit(pcity, i) && get_unit_type(i)->build_cost <= n &&
        (m == LAND_MOVING || m == SEA_MOVING) &&
        (m == which || which == 0)) {
      j = ai_unit_defence_desirability(i);
      j *= j;
      if (unit_type_flag(i, F_FIELDUNIT) && !isdef) j = 0; /* This is now confirmed */
      if (walls && m == LAND_MOVING) { j *= pcity->ai.wallvalue; j /= 10; }
      if (j > best || (j == best && get_unit_type(i)->build_cost <=
                               get_unit_type(bestid)->build_cost)) {
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
Unit_Type_id ai_choose_defender_by_type(struct city *pcity,
                                        enum unit_move_type which)
{
  return (ai_choose_defender_limited(pcity, pcity->shield_stock + 40, which));
}

/************************************************************************** 
  ...
**************************************************************************/
Unit_Type_id ai_choose_defender(struct city *pcity)
{
  return (ai_choose_defender_limited(pcity, pcity->shield_stock + 40, 0));
}

/**************************************************************************
... 
**************************************************************************/
static bool building_unwanted(struct player *plr, Impr_Type_id i)
{
  if (plr->research.researching != A_NONE)
    return FALSE;
  return (i == B_LIBRARY || i == B_UNIVERSITY || i == B_RESEARCH);
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
		       improvement_value(i)/2);
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
  if (ai_fix_unhappy(pcity) && ai_fuzzy(pplayer, TRUE))
    ai_scientists_taxmen(pcity);
  ai_sell_obsolete_buildings(pcity);
  sync_cities();
/* ai_city_choose_build(pplayer, pcity); -- moved by Syela */
}

/**************************************************************************
...
**************************************************************************/
static int ai_find_elvis_pos(struct city *pcity, int *xp, int *yp)
{
  struct government *g = get_gov_pcity(pcity);
  int foodneed, prodneed, luxneed, pwr, e, worst_value = 0;

  foodneed=(pcity->size *2) + settler_eats(pcity);
  foodneed -= pcity->food_prod; /* much more robust now -- Syela */
  prodneed = 0;
  prodneed -= pcity->shield_prod;
  luxneed = 2 * (2 * pcity->ppl_angry[4] + pcity->ppl_unhappy[4] -
		 pcity->ppl_happy[4]);
  pwr = 2 * city_tax_bonus(pcity) / 100;
  e = (luxneed + pwr - 1) / pwr;
  if (e > 1) {
    foodneed += (e - 1) * 2;
    prodneed += (e - 1);
  } /* not as good as elvising all at once, but should be adequate */

  unit_list_iterate(pcity->units_supported, punit)
    prodneed += utype_shield_cost(unit_type(punit), g);
  unit_list_iterate_end;
  
  prodneed -= citygov_free_shield(pcity, g);

  *xp = 0;
  *yp = 0;
  city_map_iterate(x, y) {
    if (is_city_center(x, y))
      continue; 
    if (get_worker_city(pcity, x, y) == C_TILE_WORKER) {
      int value = city_tile_value(pcity, x, y,
				  foodneed + city_get_food_tile(x, y, pcity),
				  prodneed + city_get_shields_tile(x, y,
								   pcity));

      if ((*xp == 0 && *yp == 0) || value < worst_value) {
	*xp = x;
	*yp = y;
	worst_value = value;
      }
    }
  } city_map_iterate_end;
  if (*xp == 0 && *yp == 0) return 0;
  foodneed += city_get_food_tile(*xp, *yp, pcity);
  prodneed += city_get_shields_tile(*xp, *yp, pcity);
  if (e > 1) {
    foodneed -= (e - 1) * 2; /* forgetting these two lines */
    prodneed -= (e - 1); /* led to remarkable idiocy -- Syela */
  }
  if (foodneed > pcity->food_stock) {
    freelog(LOG_DEBUG,
	    "No elvis_pos in %s - would create famine.", pcity->name);
    return 0; /* Bad time to Elvis */
  }
  if (prodneed > 0) {
    freelog(LOG_DEBUG,
	    "No elvis_pos in %s - would fail-to-upkeep.", pcity->name);
    return 0; /* Bad time to Elvis */
  }
  return(city_tile_value(pcity, *xp, *yp, foodneed, prodneed));
}

/**************************************************************************
...
**************************************************************************/
int ai_make_elvis(struct city *pcity)
{
  int xp, yp, val;
  if ((val = ai_find_elvis_pos(pcity, &xp, &yp)) != 0) {
    server_remove_worker_city(pcity, xp, yp);
    pcity->ppl_elvis++;
    city_refresh(pcity); /* this lets us call ai_make_elvis in luxury routine */
    return val; /* much more useful! */
  } else
    return 0;
}

/**************************************************************************
...
**************************************************************************/
static void make_elvises(struct city *pcity)
{
  int xp, yp, elviscost;
  pcity->ppl_elvis += (pcity->ppl_taxman + pcity->ppl_scientist);
  pcity->ppl_taxman = 0;
  pcity->ppl_scientist = 0;
  city_refresh(pcity);
 
  while (TRUE) {
    if ((elviscost = ai_find_elvis_pos(pcity, &xp, &yp)) != 0) {
      int food = city_get_food_tile(xp, yp, pcity);

      if (food > pcity->food_surplus)
	break;
      if (food == pcity->food_surplus && city_happy(pcity))
	break; /* scientists don't party */
      if (elviscost >= 24) /* doesn't matter if we wtbb or not! */
        break; /* no benefit here! */
      server_remove_worker_city(pcity, xp, yp);
      pcity->ppl_elvis++;
      city_refresh(pcity);
    } else
      break;
  }
    
}

/**************************************************************************
...
**************************************************************************/
static void make_taxmen(struct city *pcity)
{
  while (!city_unhappy(pcity) && pcity->ppl_elvis > 0) {
    pcity->ppl_taxman++;
    pcity->ppl_elvis--;
    city_refresh(pcity);
  }
  if (city_unhappy(pcity)) {
    pcity->ppl_taxman--;
    pcity->ppl_elvis++;
    city_refresh(pcity);
  }

}

/**************************************************************************
...
**************************************************************************/
static void make_scientists(struct city *pcity)
{
  make_taxmen(pcity); /* reuse the code */
  pcity->ppl_scientist = pcity->ppl_taxman;
  pcity->ppl_taxman = 0;
}

/**************************************************************************
 we prefer science, but if both is 0 we prefer $ 
 (out of research goal situation)
**************************************************************************/
void ai_scientists_taxmen(struct city *pcity)
{
  int science_bonus, tax_bonus;
  make_elvises(pcity);
  if (pcity->ppl_elvis == 0 || city_unhappy(pcity)) 
    return;
  tax_bonus = city_tax_bonus(pcity);
  science_bonus = city_science_bonus(pcity);
  
  if (tax_bonus > science_bonus || (city_owner(pcity)->research.researching == A_NONE)) 
    make_taxmen(pcity);
  else
    make_scientists(pcity);

  sync_cities();
}

/**************************************************************************
...
**************************************************************************/
bool ai_fix_unhappy(struct city *pcity)
{
  if (!city_unhappy(pcity))
    return TRUE;
  while (city_unhappy(pcity)) {
    if(ai_make_elvis(pcity) == 0) break;
/*     city_refresh(pcity);         moved into ai_make_elvis for utility -- Syela */
  }
  return (!city_unhappy(pcity));
}

/**************************************************************************
...
**************************************************************************/
void emergency_reallocate_workers(struct player *pplayer, struct city *pcity)
{ /* I don't care how slow this is; it will very rarely be used. -- Syela */
/* this would be a lot easier if I made ->worked a city id. */
  struct city_list minilist;
  struct packet_unit_request pack;

  freelog(LOG_VERBOSE,
	  "Emergency in %s! (%d angry, %d unhap, %d hap, %d food, %d prod)",
	  pcity->name, pcity->ppl_angry[4], pcity->ppl_unhappy[4],
	  pcity->ppl_happy[4], pcity->food_surplus, pcity->shield_surplus);
  city_list_init(&minilist);
  map_city_radius_iterate(pcity->x, pcity->y, x, y) {
    struct city *acity=map_get_tile(x,y)->worked;
    int city_map_x, city_map_y;
    bool is_valid;

    if(acity && acity!=pcity && acity->owner==pcity->owner)  {
      if (same_pos(acity->x, acity->y, x, y)) {
	/* can't stop working city center */
	continue;
      }
      freelog(LOG_DEBUG, "Availing square in %s", acity->name);
      is_valid = map_to_city_map(&city_map_x, &city_map_y, acity, x, y);
      assert(is_valid);
      server_remove_worker_city(acity, city_map_x, city_map_y);
      acity->ppl_elvis++;
      if (!city_list_find_id(&minilist, acity->id))
	city_list_insert(&minilist, acity);
    }
  } map_city_radius_iterate_end;
  auto_arrange_workers(pcity);
  if (ai_fix_unhappy(pcity) && ai_fuzzy(pplayer, TRUE))
    ai_scientists_taxmen(pcity);
  if (pcity->shield_surplus < 0 || city_unhappy(pcity) ||
      pcity->food_stock + pcity->food_surplus < 0) {
    freelog(LOG_VERBOSE, "Emergency unresolved");
  } else {
    freelog(LOG_VERBOSE, "Emergency resolved");
  }
  freelog(LOG_DEBUG, "Rearranging workers in %s", pcity->name);

  unit_list_iterate(pcity->units_supported, punit)
    if (city_unhappy(pcity) && punit->unhappiness != 0 && punit->ai.passenger == 0) {
       freelog(LOG_VERBOSE,
	       "%s's %s is unhappy and causing unrest, disbanding it",
	       pcity->name, unit_type(punit)->name);
       pack.unit_id = punit->id;
       /* in rare cases the _safe might be needed? --dwp */
       handle_unit_disband_safe(pplayer, &pack, &myiter);
       city_refresh(pcity);
       (void) ai_fix_unhappy(pcity);
     }
  unit_list_iterate_end;       

  city_list_iterate(minilist, acity) {
    city_refresh(acity); /* otherwise food total and stuff was wrong. -- Syela */
    auto_arrange_workers(pcity);
    if (ai_fix_unhappy(acity) && ai_fuzzy(pplayer, TRUE))
      ai_scientists_taxmen(acity);
    freelog(LOG_DEBUG, "Readjusting workers in %s", acity->name);
  } city_list_iterate_end;

  sync_cities();
}
