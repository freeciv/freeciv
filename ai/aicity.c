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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <player.h>
#include <city.h>
#include <game.h>
#include <unit.h>
#include <shared.h>
#include <packets.h>
#include <events.h>
#include <map.h>
#include <mapgen.h>
#include <plrhand.h>
#include <cityhand.h>
#include <citytools.h>
#include <cityturn.h>
#include <unithand.h>
#include <unitfunc.h>
#include <aitools.h>
#include <aihand.h>
#include <aiunit.h>
#include <aicity.h>
#include <advisland.h>
#include <advleader.h>
#include <advmilitary.h>
#include <advdomestic.h>
#include <gotohand.h>
#include <settlers.h>

int ai_fix_unhappy(struct city *pcity);
void ai_manage_city(struct player *pplayer, struct city *);
void ai_scientists_taxmen(struct city *pcity);

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
  int i;
  for (i = 0 ; i < U_LAST ; i ++) {
    if (unit_flag(i, F_SETTLERS) && can_build_unit(pcity, i)) {
      ai_do_build_unit(pcity, i);
      break;
    }
  }
}

/************************************************************************** 
...
**************************************************************************/

int ai_city_build_peaceful_unit(struct city *pcity)
{
  int i;
  if (is_building_other_wonder(pcity)) {
    for (i = 0 ; i < U_LAST ; i ++) {
      if (unit_flag(i, F_CARAVAN) && can_build_unit(pcity, i)) {
	ai_do_build_unit(pcity, i);
	break;
      }
    }
    if (i < U_LAST) 
      return 1;
  } else {
    if (can_build_improvement(pcity, B_CAPITAL)) {
      pcity->currently_building=B_CAPITAL;
      pcity->is_building_unit=0;
      return 1;
    }
  }
  return 0;
}

void ai_manage_buildings(struct player *pplayer)
{ /* we have just managed all our cities but not chosen build for them yet */
  int i, j, values[B_LAST], leon = 0, palace = 0, corr = 0;
  memset(values, 0, sizeof(values));
  memset(pplayer->ai.tech_want, 0, sizeof(pplayer->ai.tech_want));

  if (find_palace(pplayer) || pplayer->government == G_DEMOCRACY) palace = 1;
  city_list_iterate(pplayer->cities, pcity)
    ai_eval_buildings(pcity);
    if (!palace) corr += pcity->corruption * 8;
    for (i = 0; i < B_LAST; i++) 
      if (pcity->ai.building_want[i] > 0) values[i] += pcity->ai.building_want[i];
    if (pcity->ai.building_want[B_LEONARDO] > leon)
      leon = pcity->ai.building_want[B_LEONARDO];
  city_list_iterate_end;

/* this is a weird place to put tech advice */
  if (pplayer->government > G_DESPOTISM) {
    for (i = 0; i < B_LAST; i++) {
      j = improvement_types[i].tech_requirement;
      if (get_invention(pplayer, j) != TECH_KNOWN)
        pplayer->ai.tech_want[j] += values[i];
    }
  } /* tired of researching pottery when we need to learn Republic!! -- Syela */

  if (!game.global_advances[A_PHILOSOPHY])
    pplayer->ai.tech_want[A_PHILOSOPHY] *= 2; /* this probably isn't right -- Syela */

  city_list_iterate(pplayer->cities, pcity)
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

}

/************************************************************************** 
... change the build order.
**************************************************************************/

void ai_city_choose_build(struct player *pplayer, struct city *pcity)
{
  struct ai_choice bestchoice, curchoice;

  bestchoice.choice = A_NONE;      
  bestchoice.want   = 0;
  bestchoice.type   = 0;

/* obsolete code destroyed */
/*  military_advisor_choose_build(pplayer, pcity, &curchoice); */
/* this is now handled in manage_city thanks to our friend ->ai.choice */
/*  copy_if_better_choice(&curchoice, &bestchoice); */
  copy_if_better_choice(&pcity->ai.choice, &bestchoice);

  if (bestchoice.want <= 100) { /* soldier at 101 cannot be denied */
    domestic_advisor_choose_build(pplayer, pcity, &curchoice);
    copy_if_better_choice(&curchoice, &bestchoice);
  }

  pcity->ai.choice.choice = bestchoice.choice; /* we want to spend gold later */
  pcity->ai.choice.want = bestchoice.want; /* so that we spend it in the right city */
  pcity->ai.choice.type = bestchoice.type; /* instead of the one atop the list */

/* type 0 = building, 1 = non-mil unit, 2 = attacker, 3 = defender */

  if (bestchoice.want) { /* Note - on fallbacks, will NOT get stopped building msg */
/* printf("%s wants %s with desire %d.\n", pcity->name, (bestchoice.type ?
unit_name(bestchoice.choice) : get_improvement_name(bestchoice.choice)),
bestchoice.want); */
    if(!pcity->is_building_unit && is_wonder(pcity->currently_building) &&
      (bestchoice.type || bestchoice.choice != pcity->currently_building))
      notify_player_ex(0, pcity->x, pcity->y, E_NOEVENT,
                   "Game: The %s have stopped building The %s in %s.",
                   get_race_name_plural(pplayer->race),
                   get_imp_name_ex(pcity, pcity->currently_building),
                   pcity->name);

    if (!bestchoice.type && (pcity->is_building_unit ||
                 pcity->currently_building != bestchoice.choice) &&
                 is_wonder(bestchoice.choice)) {
      notify_player_ex(0, pcity->x, pcity->y, E_WONDER_STARTED,
                    "Game: The %s have started building The %s in %s.",
                    get_race_name_plural(city_owner(pcity)->race),
                    get_imp_name_ex(pcity, bestchoice.choice), pcity->name);
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

/* fallbacks should never happen anymore, but probably could somehow. -- Syela */
printf("Falling back - %s didn't want soldiers, settlers, or buildings.\n", pcity->name);
  
}

int ai_city_defender_value(struct city *pcity, int a_type, int d_type)            
{
  int m;
  m = get_virtual_defense_power(a_type, d_type, pcity->x, pcity->y);
  m *= unit_types[d_type].hp * unit_types[d_type].firepower;
  m /= (do_make_unit_veteran(pcity, d_type) ? 20 : 30);
  return (m * m);
}

void try_to_sell_stuff(struct player *pplayer, struct city *pcity)
{
  int id;
  for (id = 0; id < B_LAST; id++) {
    if (can_sell_building(pcity, id)) {
      really_handle_city_sell(pplayer, pcity, id);
      break;
    }
  }
}

void ai_new_spend_gold(struct player *pplayer)
{
  int buycost, id, cost, frugal = 0;
  int total, build, did_upgrade;
  struct ai_choice bestchoice;
  struct city *pcity = NULL;

  do {
    bestchoice.want = 0;
    bestchoice.type = 0;
    bestchoice.choice = 0;
    city_list_iterate(pplayer->cities, acity)
      if (acity->ai.choice.want > bestchoice.want) {
        bestchoice.choice = acity->ai.choice.choice;
        bestchoice.want = acity->ai.choice.want;
        bestchoice.type = acity->ai.choice.type;
        pcity = acity;
      }
    city_list_iterate_end;

    if (!bestchoice.want) return;
 
    if (bestchoice.type && (!frugal || bestchoice.want > 200)) { /* if we want a unit */
      buycost = city_buy_cost(pcity);
      unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
        id = can_upgrade_unittype(pplayer, punit->type);
        did_upgrade = 0;
/* printf("%s wants %s, %s -> %s\n", pcity->name, unit_types[bestchoice.choice].name, 
unit_types[punit->type].name, unit_types[id].name); */
        if (id == bestchoice.choice) { /* and I can simply upgrade a unit I already have */
/* printf("Trying to upgrade in %s.\n", pcity->name); */
          cost = unit_upgrade_price(pplayer, punit->type, id);
          if (cost < buycost) { /* and the upgrade would be cheaper */
            if (cost < pplayer->economic.gold) { /* let's just upgrade */
              pplayer->economic.gold -= cost;
              if (punit->hp==get_unit_type(punit->type)->hp)
                punit->hp=get_unit_type(id)->hp;
              notify_player(pplayer, "Game: %s upgraded to %s in %s for %d credits",
                unit_types[punit->type].name, unit_types[id].name, pcity->name, cost);
              punit->type = id;
              send_unit_info(0, punit, 0);
              send_player_info(pplayer, pplayer);
              bestchoice.want = 0; /* no reason to buy unit we already made */
              pcity->ai.choice.want = 0; /* or deal with this city again */
              did_upgrade = 1;
            }
          }
        }
        if (id == bestchoice.choice || (punit->type == U_WARRIORS &&
            bestchoice.choice == U_PHALANX) || (punit->type == U_PHALANX &&
            bestchoice.choice == U_MUSKETEERS))  {
          if (!did_upgrade) { /* might want to disband */
            build = pcity->shield_stock + (get_unit_type(punit->type)->build_cost>>1);
            total = get_unit_type(id)->build_cost;
            cost=(total-build)*2+(total-build)*(total-build)/20;
            if ((bestchoice.want <= 100 && build >= total) ||
                  (pplayer->economic.gold >= cost)) {
              notify_player(pplayer, "Game: %s disbanded in %s.",
                unit_types[punit->type].name, pcity->name);
              pcity->shield_stock+=(get_unit_type(punit->type)->build_cost/2);
              send_city_info(pplayer, pcity, 0);
              wipe_unit(pplayer, punit);
            }
          }
        }
      unit_list_iterate_end;
    }

    if (bestchoice.want > 100 ||  /* either need defense or building NOW */
        pplayer->research.researching == A_NONE) { /* nothing else to do */
      buycost = city_buy_cost(pcity);
      if (!pcity->shield_stock) ;
      else if (bestchoice.type && unit_flag(bestchoice.choice, F_SETTLERS) &&
          !city_affected_by_wonder(pcity, B_PYRAMIDS) &&
          !city_got_building(pcity, B_GRANARY) && (pcity->size < 2 ||
          pcity->food_stock < (pcity->size - 1) * game.foodbox)) ;
      else if (bestchoice.type && bestchoice.type < 3 && /* not a defender */
        pcity->shield_stock * 3 < buycost) ; /* too expensive */
      else if (bestchoice.type == 3 && pcity->size == 1 && pcity->ai.grave_danger &&
              pcity->ai.danger > (assess_defense(pcity) +
  ai_city_defender_value(pcity, pcity->ai.grave_danger->type, bestchoice.choice)) * 2 &&
               pcity->food_stock + pcity->food_surplus < 10) { /* We're DOOMED */
printf("%s is DOOMED!  Yielding to %s's %s.\n", pcity->name,
game.players[pcity->ai.grave_danger->owner].name, 
unit_types[pcity->ai.grave_danger->type].name);
         try_to_sell_stuff(pplayer, pcity); /* and don't buy stuff */
         pcity->currently_building = ai_choose_defender_limited(pcity,
                  pcity->shield_stock + pcity->shield_surplus);
      }
      else if (pplayer->economic.gold >= buycost && (!frugal || bestchoice.want > 200)) {
        really_handle_city_buy(pplayer, pcity); /* adequately tested now */
      } else if (bestchoice.type || !is_wonder(bestchoice.choice)) {
/*        printf("%s wants %s but can't afford to buy it (%d < %d).\n",
pcity->name, (bestchoice.type ? unit_name(bestchoice.choice) :
get_improvement_name(bestchoice.choice)),  pplayer->economic.gold, buycost); */
        if (bestchoice.type && !unit_flag(bestchoice.choice, F_NONMIL)) {
          if (pcity->ai.grave_danger && !assess_defense(pcity)) { /* oh dear */
            try_to_sell_stuff(pplayer, pcity);
            if (pplayer->economic.gold >= buycost) /* Phew! */
              really_handle_city_buy(pplayer, pcity);
/* don't need to waste gold here, but may as well spend our production */
            else pcity->currently_building = ai_choose_defender_limited(pcity,
                  pcity->shield_stock + pcity->shield_surplus);
          } else if (buycost > pplayer->ai.maxbuycost) pplayer->ai.maxbuycost = buycost;
/* possibly upgrade units here */
        } /* end panic subroutine */
        if (!bestchoice.type) switch (bestchoice.choice) {
          case B_CITY: /* add other things worth raising taxes for here! -- Syela */
          case B_TEMPLE:
          case B_AQUEDUCT:
          case B_COASTAL:
          case B_SAM:
            if (buycost > pplayer->ai.maxbuycost) pplayer->ai.maxbuycost = buycost;
            break;
/* granaries/harbors/wonders not worth the tax increase */
        } /* end switch */
/* I just saw Alexander save up for walls then buy a granary.  Never again! -- Syela */
        if (bestchoice.want > 200 && pplayer->ai.maxbuycost) frugal++;
      } /* end can't-afford else */
    } /* end if want > 100 */
    pcity->ai.choice.want = 0; /* not dealing with this city a second time */
  } while (bestchoice.want > 0);
}

/**************************************************************************
 cities, build order and worker allocation stuff here..
**************************************************************************/

void ai_manage_cities(struct player *pplayer)
{ 
  pplayer->ai.maxbuycost = 0;

  city_list_iterate(pplayer->cities, pcity)
    ai_manage_city(pplayer, pcity);
  city_list_iterate_end;

  ai_manage_buildings(pplayer);

  city_list_iterate(pplayer->cities, pcity)
    military_advisor_choose_build(pplayer, pcity, &pcity->ai.choice);
    establish_city_distances(pplayer, pcity); /* in advmilitary for warmap */
/* determines downtown and distance_to_wondercity, which a_c_c_b will need */
    contemplate_settling(pplayer, pcity); /* while we have the warmap handy */
  city_list_iterate_end;

  city_list_iterate(pplayer->cities, pcity)
    ai_city_choose_build(pplayer, pcity);
  city_list_iterate_end;

  ai_new_spend_gold(pplayer);

  if (get_invention(pplayer, A_CODE) != TECH_KNOWN) {
    pplayer->ai.tech_want[A_CODE] += 150 * city_list_size(&pplayer->cities) *
              tech_goal_turns(pplayer, A_CODE);
  } else if (get_invention(pplayer, A_REPUBLIC) != TECH_KNOWN) {
    pplayer->ai.tech_want[A_REPUBLIC] += city_list_size(&pplayer->cities) *
              (tech_goal_turns(pplayer, A_REPUBLIC) * 90 + 90);
/* these may need to be fudged further sometime -- Syela */
    if (get_invention(pplayer, A_MONARCHY) != TECH_KNOWN)
      pplayer->ai.tech_want[A_MONARCHY] += 150 * city_list_size(&pplayer->cities);
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

int worst_elvis_tile(struct city *pcity, int x, int y, int bx, int by, int foodneed, int prodneed)
{
  foodneed += MAX(get_food_tile(x, y, pcity), get_food_tile(bx, by, pcity));
  prodneed += MAX(get_shields_tile(x, y, pcity), get_shields_tile(bx, by, pcity));

  return(better_tile(pcity, bx, by, x, y, foodneed, prodneed));
/* backwards on purpose, because we're looking for the worst tile */
}

/************************************************************************** 
...
**************************************************************************/

int is_defender_unit(int unit_type) 
{
  return ((U_WARRIORS <= unit_type) && (unit_type <= U_MECH));
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
  return (pplayer->score.cities < expand_cities[get_race(pplayer)->expand]);  
}

/************************************************************************** 
...
**************************************************************************/
int unit_attack_desirability(int i)
{
  return(unit_desirability(i, 0));
} 

int ai_choose_attacker(struct city *pcity, enum unit_move_type which)
{ /* don't ask me why this is in aicity, I can't even remember -- Syela */
  int i;
  int best = 0;
  int bestid = 0;
  int cur;
  for (i = U_WARRIORS; i <= U_BATTLESHIP ; i++) { /* not dealing with planes yet */
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

  for (i = U_WARRIORS; i <= U_BATTLESHIP; i++) {
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

int has_a_normal_defender(struct city *pcity)
{
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    if (is_military_unit(punit) && is_ground_unit(punit)) return 1;
  unit_list_iterate_end;
  return 0;
}

int ai_choose_defender_limited(struct city *pcity, int n)
{
  int i, j, m;
  int best= 0;
  int bestid = 0;
  int walls = 1; /* just assume city_got_citywalls(pcity); in the long run -- Syela */
  int isdef = has_a_normal_defender(pcity);

  for (i = U_WARRIORS; i <= U_BATTLESHIP; i++) {
    m = unit_types[i].move_type;
    if (can_build_unit(pcity, i) && get_unit_type(i)->build_cost <= n &&
        (m == LAND_MOVING || m == SEA_MOVING)) {
      j = unit_desirability(i, 1);
      j *= j;
      if (unit_flag(i, F_FIELDUNIT) && !isdef) j = 0; /* Experimenting. -- Syela */
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

int ai_choose_defender(struct city *pcity)
{
  return (ai_choose_defender_limited(pcity, 65535));
}

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

/**************************************************************************
... 
**************************************************************************/

int building_unwanted(struct player *plr, int i)
{
  if (plr->research.researching != A_NONE)
    return 0;
  return (i == B_LIBRARY || i == B_UNIVERSITY || i == B_RESEARCH);
}

/**************************************************************************
...
**************************************************************************/

void ai_sell_obsolete_buildings(struct city *pcity)
{
  int i;
  struct player *pplayer = city_owner(pcity);
  for (i=0;i<B_LAST;i++) {
    if(city_got_building(pcity, i) 
       && !is_wonder(i) 
       && i != B_CITY /* selling city walls is really, really dumb -- Syela */
       && (wonder_replacement(pcity, i) || building_unwanted(city_owner(pcity), i))) {
      do_sell_building(pplayer, pcity, i);
      notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		       "Game: %s is selling %s (not needed) for %d", 
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
  if (ai_fix_unhappy(pcity))
    ai_scientists_taxmen(pcity);
  ai_sell_obsolete_buildings(pcity);
/* ai_city_choose_build(pplayer, pcity); -- moved by Syela */
}

/**************************************************************************
...
**************************************************************************/

int ai_find_elvis_pos(struct city *pcity, int *xp, int *yp)
{
  int x,y, foodneed, prodneed, gov;
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
    if (is_military_unit(punit)) prodneed++;
  unit_list_iterate_end;
  gov = get_government(pcity->owner);
  if (gov == G_DESPOTISM) prodneed -= pcity->size;
  if (gov == G_MONARCHY || gov == G_COMMUNISM) prodneed -= 3;

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
  if (foodneed > pcity->food_stock) {
/*    printf("No elvis_pos in %s - would create famine.\n", pcity->name); */
    return 0; /* Bad time to Elvis */
  }
  if (prodneed > 0) {
/*    printf("No elvis_pos in %s - would fail-to-upkeep.\n", pcity->name); */
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

int free_tiles(struct city *pcity)
{
  return 1;
}

/**************************************************************************
...
**************************************************************************/
void make_elvises(struct city *pcity)
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
void make_taxmen(struct city *pcity)
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
void make_scientists(struct city *pcity)
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
  int i, j;
   
printf("Emergency in %s! (%d unhap, %d hap, %d food, %d prod)\n", pcity->name,
pcity->ppl_unhappy[4], pcity->ppl_happy[4], pcity->food_surplus, pcity->shield_surplus);
  city_list_init(&minilist);
  city_list_iterate(pplayer->cities, acity)
    if (acity == pcity) continue;
    city_map_iterate(i, j) {
      if (get_worker_city(acity, i, j) != C_TILE_WORKER) continue;
/* am I nearby? */
      if (make_dx(acity->x+i-2, pcity->x) <= 2 &&
          make_dy(acity->y+j-2, pcity->y) <= 2) {
/*printf("Availing square in %s\n", acity->name);*/
        set_worker_city(acity, i, j, C_TILE_EMPTY);
        if (!city_list_find_id(&minilist, acity->id))
          city_list_insert(&minilist, acity);
      }
    }
  city_list_iterate_end;
  city_check_workers(pplayer, pcity);
  auto_arrange_workers(pcity);
  if (ai_fix_unhappy(pcity))
    ai_scientists_taxmen(pcity);
/*printf("Rearranging workers in %s\n", pcity->name);*/

  city_list_iterate(minilist, acity)
    city_refresh(acity); /* otherwise food total and stuff was wrong. -- Syela */
    city_check_workers(pplayer, acity);
    add_adjust_workers(acity);
    city_refresh(acity);
    if (ai_fix_unhappy(acity))
      ai_scientists_taxmen(acity);
/*printf("Readjusting workers in %s\n", acity->name);*/
    send_city_info(city_owner(acity), acity, 1);
  city_list_iterate_end;
}
