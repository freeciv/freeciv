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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city.h"
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
#include "unitfunc.h"
#include "unithand.h"
#include "unittools.h"

#include "advdomestic.h"
#include "advisland.h"
#include "advleader.h"
#include "advmilitary.h"
#include "aihand.h"
#include "aitools.h"
#include "aiunit.h"

#include "aicity.h"

#ifdef UNUSED
/************************************************************************** 
...
**************************************************************************/
void ai_do_build_unit(struct city *pcity, int unit_type)
{
  pcity->is_building_unit = 1;
  pcity->currently_building = unit_type;
}

void ai_city_build_defense(struct city *pcity)
{
  ai_do_build_unit(pcity, ai_choose_defender(pcity));
}

/************************************************************************** 
...
**************************************************************************/
void ai_city_build_settler(struct city *pcity)
{
  ai_do_build_unit(pcity, best_role_unit(pcity, F_SETTLERS));
}

/************************************************************************** 
...
**************************************************************************/
int ai_city_build_peaceful_unit(struct city *pcity)
{
  int i;
  if (is_building_other_wonder(pcity)) {
    i = best_role_unit(pcity, F_CARAVAN);
    if (i < U_LAST) {
      ai_do_build_unit(pcity, i);
      return 1;
    }
  } else {
    if (can_build_improvement(pcity, B_CAPITAL)) {
      pcity->currently_building=B_CAPITAL;
      pcity->is_building_unit=0;
      return 1;
    }
  }
  return 0;
}
#endif /* UNUSED */

/************************************************************************** 
...
**************************************************************************/
static void ai_manage_buildings(struct player *pplayer)
{ /* we have just managed all our cities but not chosen build for them yet */
  struct government *g = get_gov_pplayer(pplayer);
  int i, j, values[B_LAST], leon = 0, palace = 0, corr = 0;
  memset(values, 0, sizeof(values));
  memset(pplayer->ai.tech_want, 0, sizeof(pplayer->ai.tech_want));

  if (find_palace(pplayer) || g->corruption_level == 0) palace = 1;
  city_list_iterate(pplayer->cities, pcity)
    ai_eval_buildings(pcity);
    if (!palace) corr += pcity->corruption * 8;
    for (i = 0; i < B_LAST; i++) 
      if (pcity->ai.building_want[i] > 0) values[i] += pcity->ai.building_want[i];
    if (pcity->ai.building_want[B_LEONARDO] > leon)
      leon = pcity->ai.building_want[B_LEONARDO];
  city_list_iterate_end;

/* this is a weird place to iterate a units list! */
  unit_list_iterate(pplayer->units, punit)
    if (is_sailing_unit(punit))
      values[B_MAGELLAN] += unit_types[punit->type].build_cost * 6 /
                            unit_types[punit->type].move_rate;
  unit_list_iterate_end;
  values[B_MAGELLAN] *= 100 * SHIELD_WEIGHTING;
  values[B_MAGELLAN] /= (MORT * improvement_value(B_MAGELLAN));

  /* This is a weird place to put tech advice */
  /* This was: > G_DESPOTISM; should maybe remove test, depending
   * on new government evaluation etc, but used for now for
   * regression testing --dwp */
  if (g->index != game.default_government
      && g->index != game.government_when_anarchy) {
    for (i = 0; i < B_LAST; i++) {
      j = improvement_types[i].tech_requirement;
      if (get_invention(pplayer, j) != TECH_KNOWN)
        pplayer->ai.tech_want[j] += values[i];
    }
  } /* tired of researching pottery when we need to learn Republic!! -- Syela */

  /* was A_PHILOSOPHY: */
  if (!game.global_advances[game.rtech.get_bonus_tech])
    pplayer->ai.tech_want[game.rtech.get_bonus_tech] *= 2;
      /* this probably isn't right -- Syela */

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
    for (i = 0; i < B_LAST; i++) {
      if (!pcity->is_building_unit && is_wonder(i) &&
          is_wonder(pcity->currently_building))
        pcity->ai.building_want[i] += pcity->shield_stock / 2;
    } /* this should encourage completion of wonders, I hope! -- Syela */
  city_list_iterate_end;
}

/************************************************************************** 
... change the build order.
**************************************************************************/
static void ai_city_choose_build(struct player *pplayer, struct city *pcity)
{
  struct ai_choice bestchoice, curchoice;

  bestchoice.choice = A_NONE;      
  bestchoice.want   = 0;
  bestchoice.type   = 0;

  if( is_barbarian(pplayer) ) {    /* always build best attack unit */
    int i, iunit, bestunit = -1, bestattack = 0;
    for(i = 0; i < num_role_units(L_BARBARIAN_BUILD); i++) {
      iunit = get_role_unit(L_BARBARIAN_BUILD, i);
      if (get_unit_type(iunit)->attack_strength > bestattack) {
	bestunit = iunit;
	bestattack = get_unit_type(iunit)->attack_strength;
      }
    }
    for(i = 0; i < num_role_units(L_BARBARIAN_BUILD_TECH); i++) {
      iunit = get_role_unit(L_BARBARIAN_BUILD_TECH, i);
      if (game.global_advances[get_unit_type(iunit)->tech_requirement]
	  && get_unit_type(iunit)->attack_strength > bestattack) {
	bestunit = iunit;
	bestattack = get_unit_type(iunit)->attack_strength;
      }
    }
    if (bestunit != -1) {
      bestchoice.choice = bestunit;
      bestchoice.want   = 101;
      bestchoice.type   = 2;
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

    if (bestchoice.want <= 100 || !pcity->ai.urgency) { /* soldier at 101 cannot be denied */
      domestic_advisor_choose_build(pplayer, pcity, &curchoice);
      copy_if_better_choice(&curchoice, &bestchoice);
    }

    pcity->ai.choice.choice = bestchoice.choice; /* we want to spend gold later */
    pcity->ai.choice.want = bestchoice.want; /* so that we spend it in the right city */
    pcity->ai.choice.type = bestchoice.type; /* instead of the one atop the list */
  }

/* type 0 = building, 1 = non-mil unit, 2 = attacker, 3 = defender */

  if (bestchoice.want) { /* Note - on fallbacks, will NOT get stopped building msg */
    freelog(LOG_DEBUG, "%s wants %s with desire %d.", pcity->name,
		  (bestchoice.type ? unit_name(bestchoice.choice)
		   : get_improvement_name(bestchoice.choice)),
		  bestchoice.want);
    if(!pcity->is_building_unit && is_wonder(pcity->currently_building) &&
      (bestchoice.type || bestchoice.choice != pcity->currently_building))
      notify_player_ex(0, pcity->x, pcity->y, E_WONDER_STOPPED,
                   _("Game: The %s have stopped building The %s in %s."),
                   get_nation_name_plural(pplayer->nation),
                   get_impr_name_ex(pcity, pcity->currently_building),
                   pcity->name);

    if (!bestchoice.type && (pcity->is_building_unit ||
                 pcity->currently_building != bestchoice.choice) &&
                 is_wonder(bestchoice.choice)) {
      notify_player_ex(0, pcity->x, pcity->y, E_WONDER_STARTED,
                    _("Game: The %s have started building The %s in %s."),
                    get_nation_name_plural(city_owner(pcity)->nation),
                    get_impr_name_ex(pcity, bestchoice.choice), pcity->name);
      pcity->currently_building = bestchoice.choice;
      pcity->is_building_unit    = (bestchoice.type > 0);
      generate_warmap(pcity, 0); /* need to establish distance to wondercity */
      establish_city_distances(pplayer, pcity); /* for caravans in other cities */
    } else {
      pcity->currently_building = bestchoice.choice;
      pcity->is_building_unit    = (bestchoice.type > 0);
    }

    return;
  } /* AI cheats -- no penalty for switching from unit to improvement, etc. */

  /* fallbacks should never happen anymore, but probably
     could somehow. -- Syela */
  freelog(LOG_VERBOSE, "Falling back - %s didn't want soldiers, settlers,"
                       " or buildings", pcity->name);
  if (can_build_improvement(pcity, B_BARRACKS)) {
    pcity->currently_building = B_BARRACKS;
    pcity->is_building_unit = 0;
  } else {
    pcity->currently_building = best_role_unit(pcity, F_SETTLERS); /* yes, this could be truly bad */
    pcity->is_building_unit = 1;
  }
/* I think fallbacks only happen with techlevel 0, and even then are rare.
I haven't seen them, but I want to somewhat prepare for them anyway. -- Syela */  
}

/************************************************************************** 
...
**************************************************************************/
#ifdef GRAVEDANGERWORKS
static int ai_city_defender_value(struct city *pcity, int a_type, int d_type)
{
  int m;
  m = get_virtual_defense_power(a_type, d_type, pcity->x, pcity->y);
  if (do_make_unit_veteran(pcity, d_type)) m *= 1.5;
  m *= unit_types[d_type].hp * unit_types[d_type].firepower;
  m /= 30;
  return (m * m);
}
#endif

/************************************************************************** 
...
**************************************************************************/
static void try_to_sell_stuff(struct player *pplayer, struct city *pcity)
{
  int id;
  for (id = 0; id < B_LAST; id++) {
    if (can_sell_building(pcity, id) && id != B_CITY) {
/* selling walls to buy defenders is counterproductive -- Syela */
      really_handle_city_sell(pplayer, pcity, id);
      break;
    }
  }
}

/************************************************************************** 
...
**************************************************************************/
static void ai_new_spend_gold(struct player *pplayer)
{
  int buycost, id, cost, frugal = 0, trireme=0;
  int total, build, did_upgrade, reserve;
  struct ai_choice bestchoice;
  struct city *pcity = NULL;

  reserve=ai_gold_reserve(pplayer);

  do {
    if (reserve > pplayer->economic.gold)
      break;

    bestchoice.want = 0;
    bestchoice.type = 0;
    bestchoice.choice = 0;
    city_list_iterate(pplayer->cities, acity)
      if (acity->anarchy) continue;
      if (acity->ai.choice.want > bestchoice.want && ai_fuzzy(pplayer,1)) {
        bestchoice.choice = acity->ai.choice.choice;
        bestchoice.want = acity->ai.choice.want;
        bestchoice.type = acity->ai.choice.type;
        pcity = acity;
      }
    city_list_iterate_end;

    if (!bestchoice.want) break;
 
    if (bestchoice.type && (!frugal || bestchoice.want > 200)) { /* if we want a unit */
      buycost = city_buy_cost(pcity);
      unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
        id = can_upgrade_unittype(pplayer, punit->type);
        did_upgrade = 0;
	freelog(LOG_DEBUG, "%s wants %s, %s -> %s", pcity->name,
		unit_types[bestchoice.choice].name, 
		unit_types[punit->type].name,
		(id>=0 ? unit_types[id].name : "-1"));
        if (id == bestchoice.choice && ai_fuzzy(pplayer,1)) {
	  /* and I can simply upgrade a unit I already have */
	  freelog(LOG_DEBUG, "Trying to upgrade in %s.", pcity->name);
          cost = unit_upgrade_price(pplayer, punit->type, id);
          if (cost < buycost) { /* and the upgrade would be cheaper */
            if (cost < pplayer->economic.gold) { /* let's just upgrade */
              pplayer->economic.gold -= cost;
              notify_player(pplayer,
			    _("Game: %s upgraded to %s in %s for %d credits."),
			    unit_types[punit->type].name, unit_types[id].name,
			    pcity->name, cost);
	      upgrade_unit(punit, id);
              send_player_info(pplayer, pplayer);
              bestchoice.want = 0; /* no reason to buy unit we already made */
              pcity->ai.choice.want = 0; /* or deal with this city again */
              did_upgrade = 1;
            }
          }
        }
	if ((id == bestchoice.choice
	     || (unit_has_role(bestchoice.choice, L_DEFEND_GOOD)
		 && is_on_unit_upgrade_path(bestchoice.choice, punit->type)))
	    && ai_fuzzy(pplayer, 1)) {
          if (!did_upgrade) { /* might want to disband */
            build = pcity->shield_stock + (get_unit_type(punit->type)->build_cost/2);
            total = get_unit_type(bestchoice.choice)->build_cost;
            cost=(total-build)*2+(total-build)*(total-build)/20;
            if ((bestchoice.want <= 100 && build >= total) ||
                  (pplayer->economic.gold >= cost)) {
	      freelog(LOG_VERBOSE, "%s disbanding %s in %s to help get %s",
		   pplayer->name, unit_types[punit->type].name, pcity->name,
		   unit_types[bestchoice.choice].name);
              notify_player(pplayer, _("Game: %s disbanded in %s."),
			    unit_types[punit->type].name, pcity->name);
	      /* FIXME: should be handled in server... */
              pcity->shield_stock+=(get_unit_type(punit->type)->build_cost/2);
              send_city_info(pplayer, pcity);
              wipe_unit_safe(punit, &myiter);
            }
          }
        }
      unit_list_iterate_end;
    }

    if (bestchoice.want > 100 ||  /* either need defense or building NOW */
        pplayer->research.researching == A_NONE) { /* nothing else to do */
      buycost = city_buy_cost(pcity);
      if (!pcity->shield_stock) ;
      else if (!bestchoice.type && is_wonder(bestchoice.choice) &&
               buycost >= 200) ; /* wait for more vans */
      else if (bestchoice.type && unit_flag(bestchoice.choice, F_SETTLERS) &&
          !city_got_effect(pcity, B_GRANARY) && (pcity->size < 2 ||
         pcity->food_stock < pcity->size * game.foodbox)) ;
      else if (bestchoice.type && bestchoice.type < 3 && /* not a defender */
        buycost > unit_types[bestchoice.choice].build_cost * 2) { /* too expensive */
        if (unit_flag(bestchoice.choice, F_CARAVAN) &&
            pplayer->ai.maxbuycost < 100) pplayer->ai.maxbuycost = 100;
      }
#ifdef GRAVEDANGERWORKS
      else if (bestchoice.type == 3 && pcity->size == 1 &&
        pcity->ai.grave_danger > (assess_defense_quadratic(pcity) +
  ai_city_defender_value(pcity, U_LEGION, bestchoice.choice)) * 2 &&
               pcity->food_stock + pcity->food_surplus < 10) { /* We're DOOMED */
	 freelog(LOG_VERBOSE, "%s is DOOMED!", pcity->name);
         try_to_sell_stuff(pplayer, pcity); /* and don't buy stuff */
         pcity->currently_building = ai_choose_defender_limited(pcity,
                  pcity->shield_stock + pcity->shield_surplus, 0);
      }
#endif
      else if ((pplayer->economic.gold-pplayer->ai.est_upkeep) >= buycost
	      && (!frugal || bestchoice.want > 200)) {
	if (ai_fuzzy(pplayer,1) || (pcity->ai.grave_danger
				    && !assess_defense(pcity))) {
	  really_handle_city_buy(pplayer, pcity); /* adequately tested now */
	}
      } else if (bestchoice.type || !is_wonder(bestchoice.choice)) {
	freelog(LOG_DEBUG, "%s wants %s but can't afford to buy it"
		      " (%d < %d).", pcity->name,
		      (bestchoice.type ? unit_name(bestchoice.choice)
		       : get_improvement_name(bestchoice.choice)),
		      pplayer->economic.gold, buycost);
        if (bestchoice.type && !unit_flag(bestchoice.choice, F_NONMIL)) {
          if (pcity->ai.grave_danger && !assess_defense(pcity)) { /* oh dear */
            try_to_sell_stuff(pplayer, pcity);
            if ((pplayer->economic.gold-pplayer->ai.est_upkeep) >= buycost)
              really_handle_city_buy(pplayer, pcity);
/* don't need to waste gold here, but may as well spend our production */
            else pcity->currently_building = ai_choose_defender_limited(pcity,
                  pcity->shield_stock + pcity->shield_surplus, 0);
          } else if (buycost > pplayer->ai.maxbuycost) pplayer->ai.maxbuycost = buycost;
/* possibly upgrade units here */
        } /* end panic subroutine */
        if (bestchoice.type && unit_flag(bestchoice.choice, F_CARAVAN))
            if (buycost > pplayer->ai.maxbuycost) pplayer->ai.maxbuycost = buycost;
/* Gudy reminded me AI was slow to build wonders, I thought of the above -- Syela */
        if (!bestchoice.type) switch (bestchoice.choice) {
          case B_AQUEDUCT:
            if (city_happy(pcity)) break;
        /* PASS THROUGH */
          case B_CITY: /* add other things worth raising taxes for here! -- Syela */
          case B_TEMPLE:
          case B_COASTAL:
          case B_SAM:
          case B_SDI:
          case B_PALACE:
          case B_COLOSSEUM:
          case B_CATHEDRAL:
            if (buycost > pplayer->ai.maxbuycost) pplayer->ai.maxbuycost = buycost;
            break;
/* granaries/harbors/wonders not worth the tax increase */
        } /* end switch */
/* I just saw Alexander save up for walls then buy a granary.  Never again! -- Syela */
/*        if (bestchoice.want > 200 && pplayer->ai.maxbuycost) frugal++; */
        if (pplayer->ai.maxbuycost) frugal++; /* much more frugal -- Syela */
      } /* end can't-afford else */
    } /* end if want > 100 */
    pcity->ai.choice.want = 0; /* not dealing with this city a second time */
  } while (bestchoice.want > 0);
/* always upgrade triremes, since they vanish and are FIELDUNITS */
  if (num_role_units(F_TRIREME)==0)
    id = -1;
  else {
    trireme = get_role_unit(F_TRIREME, 0);
    id = can_upgrade_unittype(pplayer, trireme);
  }
  if (id > 0 && !frugal) {
    unit_list_iterate(pplayer->units, punit)
      if (punit->type != trireme) continue;
      pcity = map_get_city(punit->x, punit->y);
      if (!pcity) continue;
      cost = unit_upgrade_price(pplayer, punit->type, id);
      if (cost < pplayer->economic.gold && ai_fuzzy(pplayer,1)) {
	/* let's just upgrade */
        pplayer->economic.gold -= cost;
	upgrade_unit(punit, id);
        notify_player(pplayer,
		      _("Game: %s upgraded to %s in %s for %d credits."),
		      unit_types[punit->type].name, unit_types[id].name,
		      pcity->name, cost);
        send_player_info(pplayer, pplayer);
      } /* end if upgrade */
    unit_list_iterate_end;
  } /* end if */
}

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
    contemplate_settling(pplayer, pcity); /* while we have the warmap handy */
/* seacost may have been munged if we found a boat, but if we found a boat
we don't rely on the seamap being current since we will recalculate. -- Syela */

  city_list_iterate_end;

  city_list_iterate(pplayer->cities, pcity)
    ai_city_choose_build(pplayer, pcity);
  city_list_iterate_end;

  ai_new_spend_gold(pplayer);

  /* use ai_gov_tech_hints: */
  for(i=0; i<MAX_NUM_TECH_LIST; i++) {
    struct ai_gov_tech_hint *hint = &ai_gov_tech_hints[i];

    if (hint->tech == A_LAST)
      break;
    if (get_invention(pplayer, hint->tech) != TECH_KNOWN) {
      pplayer->ai.tech_want[hint->tech] += city_list_size(&pplayer->cities)
	* (hint->turns_factor * tech_goal_turns(pplayer, hint->tech)
	   + hint->const_factor);
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
int city_get_buildings(struct city *pcity)
{
  int b=0;
  int i;
  for (i=0; i<B_LAST; i++) {
    if (is_wonder(i)) continue;
    if (i==B_PALACE)  continue;
    if (city_got_building(pcity, i))
      b++;
  }
  return b;
}

/**************************************************************************
... find a good (bad) tile to remove
**************************************************************************/
static int worst_elvis_tile(struct city *pcity, int x, int y, int bx, int by,
			    int foodneed, int prodneed)
{
  int a, b;
  a = city_tile_value(pcity, x, y,
      foodneed + get_food_tile(x, y, pcity),
      prodneed + get_shields_tile(x, y, pcity));
  b = city_tile_value(pcity, bx, by,
      foodneed + get_food_tile(bx, by, pcity),
      prodneed + get_shields_tile(bx, by, pcity));
  return (a < b);
}

/************************************************************************** 
...
**************************************************************************/
static int is_defender_unit(int unit_type) 
{
  return unit_has_role(unit_type, L_DEFEND_GOOD)
      || unit_has_role(unit_type, L_DEFEND_OK);
  /* return ((U_WARRIORS <= unit_type) && (unit_type <= U_MECH)); */
}

/************************************************************************** 
...
**************************************************************************/
int city_get_defenders(struct city *pcity)
{
  int def=0;
  unit_list_iterate(pcity->units_supported, punit) {
    if (!can_build_unit_direct(pcity, punit->type))
      continue;
    if (!is_defender_unit(punit->type))
      continue;
    if (punit->x==pcity->x && punit->y==pcity->y)
      def++;
  }
  unit_list_iterate_end;
  return def;
}

/************************************************************************** 
...
**************************************************************************/
int city_get_settlers(struct city *pcity)
{
  int set=0;
  unit_list_iterate(pcity->units_supported, punit) {
    if (unit_flag(punit->type, F_SETTLERS))
      set++;
  }
  unit_list_iterate_end;
  return set;
}

/************************************************************************** 
...
**************************************************************************/
int ai_in_initial_expand(struct player *pplayer)
{
  int expand_cities [3] = {3, 5, 7};
  return (pplayer->score.cities < expand_cities[get_nation_by_plr(pplayer)->expand]);  
}

/************************************************************************** 
...
**************************************************************************/
int unit_attack_desirability(int i)
{
  return(unit_desirability(i, 0));
} 

void ai_choose_ferryboat(struct player *pplayer, struct city *pcity, struct ai_choice *choice)
{
  ai_choose_role_unit(pplayer, pcity, choice, L_FERRYBOAT,  choice->want);
}

/* don't ask me why this is in aicity, I can't even remember -- Syela */
static int ai_choose_attacker(struct city *pcity, enum unit_move_type which)
{ 
  int i;
  int best = 0;
  int bestid = 0;
  int cur;
  for (i = 0; i < game.num_unit_types; i++) {
    /* not dealing with planes yet */
    if (!is_ai_simple_military(i)) continue;
    cur = unit_attack_desirability(i);
    if (which == unit_types[i].move_type) {
      if (can_build_unit(pcity, i) && (cur > best || (cur == best &&
 get_unit_type(i)->build_cost <= get_unit_type(bestid)->build_cost))) {
        best = cur;
        bestid = i;
      }
    }
  }
  return bestid;
}

int ai_choose_attacker_ground(struct city *pcity)
{
  return(ai_choose_attacker(pcity, LAND_MOVING));
}

int ai_choose_attacker_sailing(struct city *pcity)
{
  return(ai_choose_attacker(pcity, SEA_MOVING));
}

int ai_choose_defender_versus(struct city *pcity, int v)
{
  int i, j, m;
  int best= 0;
  int bestid = 0;

  for (i = 0; i < game.num_unit_types; i++) {
    if (!is_ai_simple_military(i)) continue;
    m = unit_types[i].move_type;
    if (can_build_unit(pcity, i) && (m == LAND_MOVING || m == SEA_MOVING)) {
      j = get_virtual_defense_power(v, i, pcity->x, pcity->y);
      if (j > best || (j == best && get_unit_type(i)->build_cost <=
                               get_unit_type(bestid)->build_cost)) {
        best = j;
        bestid = i;
      }
    }
  }
  return bestid;
}

/* I'm not sure how to generalize this for rulesets, because I
   don't understand what its trying to return.  A military ground
   unit with hp>10 could be a Cannon, which doesn't seem like
   a "normal defender" to me...   But I think I have to do something,
   because for the Civ1 ruleset, all units have hp==10, so this
   would return 0.  Hence this strange a+2*d formula, which gives
   the same results as (hp>10) for the default set, and does
   _something_ for other sets.  This should be revisited.  --dwp
*/
int has_a_normal_defender(struct city *pcity)
{
#if 0 /* pre-rulesets */
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    if (is_military_unit(punit) && is_ground_unit(punit) &&
        unit_types[punit->type].hp > 10 && /* for when we don't get Feudalism */
        can_build_unit(pcity, punit->type)) return 1;
  unit_list_iterate_end;
#else
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    if (is_military_unit(punit)
	&& is_ground_unit(punit)
	&& (unit_types[punit->type].attack_strength
	    + 2*unit_types[punit->type].defense_strength) >= 9
	&& can_build_unit(pcity, punit->type)) {
      return 1;
    }
  unit_list_iterate_end;
#endif
  return 0;
}

int ai_choose_defender_limited(struct city *pcity, int n, enum unit_move_type which)
{
  int i, j, m;
  int best= 0;
  int bestid = 0;
  int walls = 1; /* just assume city_got_citywalls(pcity); in the long run -- Syela */
  int isdef = has_a_normal_defender(pcity);

  for (i = 0; i < game.num_unit_types; i++) {
    if (!is_ai_simple_military(i)) continue;
    m = unit_types[i].move_type;
    if (can_build_unit(pcity, i) && get_unit_type(i)->build_cost <= n &&
        (m == LAND_MOVING || m == SEA_MOVING) &&
        (m == which || !which)) {
      j = unit_desirability(i, 1);
      j *= j;
      if (unit_flag(i, F_FIELDUNIT) && !isdef) j = 0; /* This is now confirmed */
      if (walls && m == LAND_MOVING) { j *= pcity->ai.wallvalue; j /= 10; }
      if (j > best || (j == best && get_unit_type(i)->build_cost <=
                               get_unit_type(bestid)->build_cost)) {
        best = j;
        bestid = i;
      }
    }
  }
  return bestid;
}

int ai_choose_defender_by_type(struct city *pcity, enum unit_move_type which)
{
  return (ai_choose_defender_limited(pcity, pcity->shield_stock + 40, which));
}

int ai_choose_defender(struct city *pcity)
{
  return (ai_choose_defender_limited(pcity, pcity->shield_stock + 40, 0));
}

#ifdef UNUSED
/************************************************************************** 
...
**************************************************************************/
void adjust_build_choice(struct player *pplayer, struct ai_choice *cur, 
			 struct ai_choice *best, int advisor)
{
  island_adjust_build_choice(pplayer, cur, advisor);
  leader_adjust_build_choice(pplayer, cur, advisor);
  copy_if_better_choice(cur, best);
}
#endif /* UNUSED */

/**************************************************************************
... 
**************************************************************************/

static int building_unwanted(struct player *plr, int i)
{
  if (plr->research.researching != A_NONE)
    return 0;
  return (i == B_LIBRARY || i == B_UNIVERSITY || i == B_RESEARCH);
}

/**************************************************************************
...
**************************************************************************/

static void ai_sell_obsolete_buildings(struct city *pcity)
{
  int i;
  struct player *pplayer = city_owner(pcity);
  for (i=0;i<B_LAST;i++) {
    if(city_got_building(pcity, i) 
       && !is_wonder(i) 
       && i != B_CITY /* selling city walls is really, really dumb -- Syela */
       && (wonder_replacement(pcity, i) || building_unwanted(city_owner(pcity), i))) {
      do_sell_building(pplayer, pcity, i);
      notify_player_ex(pplayer, pcity->x, pcity->y, E_IMP_SOLD,
		       _("Game: %s is selling %s (not needed) for %d."), 
		       pcity->name, get_improvement_name(i), 
		       improvement_value(i)/2);
      return; /* max 1 building each turn */
    }
  }
}

/**************************************************************************
 cities, build order and worker allocation stuff here..
**************************************************************************/
void ai_manage_city(struct player *pplayer, struct city *pcity)
{
  city_check_workers(pplayer, pcity); /* no reason not to, many reasons to do so! */
  auto_arrange_workers(pcity);
  if (ai_fix_unhappy(pcity) && ai_fuzzy(pplayer,1))
    ai_scientists_taxmen(pcity);
  ai_sell_obsolete_buildings(pcity);
/* ai_city_choose_build(pplayer, pcity); -- moved by Syela */
}

/**************************************************************************
...
**************************************************************************/

static int ai_find_elvis_pos(struct city *pcity, int *xp, int *yp)
{
  struct government *g = get_gov_pcity(pcity);
  int x,y, foodneed, prodneed;
  int luxneed, pwr, e;

  foodneed=(pcity->size *2) + settler_eats(pcity);
  foodneed -= pcity->food_prod; /* much more robust now -- Syela */
  prodneed = 0;
  prodneed -= pcity->shield_prod;
  luxneed = 2 * (pcity->ppl_unhappy[4] - pcity->ppl_happy[4]);
  pwr = 2 * city_tax_bonus(pcity) / 100;
  e = (luxneed + pwr - 1) / pwr;
  if (e > 1) {
    foodneed += (e - 1) * 2;
    prodneed += (e - 1);
  } /* not as good as elvising all at once, but should be adequate */

  unit_list_iterate(pcity->units_supported, punit)
    prodneed += utype_shield_cost(get_unit_type(punit->type), g);
  unit_list_iterate_end;
  
  prodneed -= citygov_free_shield(pcity, g);

  *xp = 0;
  *yp = 0;
  city_map_iterate(x, y) {
    if (x==2 && y==2)
      continue; 
    if (get_worker_city(pcity, x, y) == C_TILE_WORKER) {
      if (*xp==0 && *yp==0) { 
	*xp=x;
	*yp=y;
      } else {
	if (worst_elvis_tile(pcity, x, y, *xp, *yp, foodneed, prodneed)) {
	  *xp=x;
	  *yp=y;
	}
      }
    }
  }
  if (*xp == 0 && *yp == 0) return 0;
  foodneed += get_food_tile(*xp, *yp, pcity);
  prodneed += get_shields_tile(*xp, *yp, pcity);
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
  if ((val = ai_find_elvis_pos(pcity, &xp, &yp))) {
    set_worker_city(pcity, xp, yp, C_TILE_EMPTY);
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
 
  while (1) {
    if ((elviscost = ai_find_elvis_pos(pcity, &xp, &yp))) {
      if (get_food_tile(xp, yp, pcity) > pcity->food_surplus)
	break;
      if (get_food_tile(xp, yp, pcity) == pcity->food_surplus && city_happy(pcity))
	break; /* scientists don't party */
      if (elviscost >= 24) /* doesn't matter if we wtbb or not! */
        break; /* no benefit here! */
      set_worker_city(pcity, xp, yp, C_TILE_EMPTY);
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
  while (!city_unhappy(pcity) && pcity->ppl_elvis) {
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
}

/**************************************************************************
...
**************************************************************************/
int ai_fix_unhappy(struct city *pcity)
{
  if (!city_unhappy(pcity))
    return 1;
  while (city_unhappy(pcity)) {
    if(!ai_make_elvis(pcity)) break;
/*     city_refresh(pcity);         moved into ai_make_elvis for utility -- Syela */
  }
  return (!city_unhappy(pcity));
}

void emergency_reallocate_workers(struct player *pplayer, struct city *pcity)
{ /* I don't care how slow this is; it will very rarely be used. -- Syela */
/* this would be a lot easier if I made ->worked a city id. */
  struct city_list minilist;
  struct packet_unit_request pack;
  int i, j;
   
  freelog(LOG_VERBOSE, "Emergency in %s! (%d unhap, %d hap, %d food, %d prod)",
       pcity->name, pcity->ppl_unhappy[4], pcity->ppl_happy[4],
       pcity->food_surplus, pcity->shield_surplus);
  city_list_init(&minilist);
  city_map_iterate(i, j) {
    int x=map_adjust_x(pcity->x+i-2), y=map_adjust_y(pcity->y+j-2);
    struct city *acity=map_get_tile(x,y)->worked;
    if(acity!=NULL && acity!=pcity && acity->owner==pcity->owner)  {
      if(acity->x==x && acity->y==y) /* can't stop working city center */
	continue;  
      freelog(LOG_DEBUG, "Availing square in %s", acity->name);
      set_worker_city(acity, map_to_city_x(acity, x), 
                      map_to_city_y(acity, y), C_TILE_EMPTY);
      if (!city_list_find_id(&minilist, acity->id))
	city_list_insert(&minilist, acity);
    }
  }
  city_check_workers(pplayer, pcity);
  auto_arrange_workers(pcity);
  if (ai_fix_unhappy(pcity) && ai_fuzzy(pplayer,1))
    ai_scientists_taxmen(pcity);
  if (pcity->shield_surplus < 0 || city_unhappy(pcity) ||
      pcity->food_stock + pcity->food_surplus < 0) {
    freelog(LOG_VERBOSE, "Emergency unresolved");
  } else {
    freelog(LOG_VERBOSE, "Emergency resolved");
  }
  freelog(LOG_DEBUG, "Rearranging workers in %s", pcity->name);

  unit_list_iterate(pcity->units_supported, punit)
    if (city_unhappy(pcity) && punit->unhappiness && !punit->ai.passenger) {
       freelog(LOG_VERBOSE,
	       "%s's %s is unhappy and causing unrest, disbanding it",
	       pcity->name, unit_types[punit->type].name);
       pack.unit_id = punit->id;
       /* in rare cases the _safe might be needed? --dwp */
       handle_unit_disband_safe(pplayer, &pack, &myiter);
       city_refresh(pcity);
       ai_fix_unhappy(pcity);
     }
  unit_list_iterate_end;       

  city_list_iterate(minilist, acity)
    city_refresh(acity); /* otherwise food total and stuff was wrong. -- Syela */
    city_check_workers(pplayer, acity);
    add_adjust_workers(acity);
    city_refresh(acity);
    if (ai_fix_unhappy(acity) && ai_fuzzy(pplayer,1))
      ai_scientists_taxmen(acity);
    freelog(LOG_DEBUG, "Readjusting workers in %s", acity->name);
    /* FIXME: should be handled seperately, and in server... */
    send_city_info(city_owner(acity), acity);
  city_list_iterate_end;
}
