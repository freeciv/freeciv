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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "combat.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "path_finding.h"
#include "pf_tools.h"
#include "player.h"
#include "rand.h"
#include "shared.h"
#include "timing.h"
#include "unit.h"

#include "barbarian.h"
#include "citytools.h"
#include "cityturn.h"
#include "diplomats.h"
#include "gotohand.h"
#include "maphand.h"
#include "settlers.h"
#include "unithand.h"
#include "unittools.h"

#include "advmilitary.h"
#include "aicity.h"
#include "aihand.h"
#include "ailog.h"
#include "aitools.h"
#include "aiunit.h"

#include "aidiplomat.h"

#define LOG_DIPLOMAT LOG_DEBUG

static void find_city_to_diplomat(struct player *pplayer, struct unit *punit,
                                  struct city **ctarget, int *move_dist);

/******************************************************************************
  Number of improvements that can be sabotaged in pcity.
******************************************************************************/
static int count_sabotagable_improvements(struct city *pcity)
{
  int count = 0;

  built_impr_iterate(pcity, index) {
    if (!is_wonder(index) && index != B_PALACE) {
      count++;
    }
  } built_impr_iterate_end;

  return count;
}

/******************************************************************************
  Number of techs that we don't have and the enemy (tplayer) does.
******************************************************************************/
static int count_stealable_techs(struct player *pplayer, struct player *tplayer)
{
  int index, count = 0;

  for (index = A_FIRST; index < game.num_tech_types; index++) {
    if ((get_invention(pplayer, index) != TECH_KNOWN)
        && (get_invention(tplayer, index) == TECH_KNOWN)) {
      count++;
    }
  }

  return count;
}

/**********************************************************************
  Calculates our need for diplomats as defensive units. May replace
  values in choice. The values 16000 and 3000 used below are totally
  arbitrary but seem to work.
***********************************************************************/
void ai_choose_diplomat_defensive(struct player *pplayer,
                                  struct city *pcity,
                                  struct ai_choice *choice, int def)
{
  /* Build a diplomat if our city is threatened by enemy diplomats, and
     we have other defensive troops, and we don't already have a diplomat
     to protect us. If we see an enemy diplomat and we don't have diplomat
     tech... race it! */
  if (def != 0 && pcity->ai.diplomat_threat && !pcity->ai.has_diplomat) {
    Unit_Type_id u = best_role_unit(pcity, F_DIPLOMAT);

    if (u < U_LAST) {
       freelog(LOG_DIPLOMAT, "A defensive diplomat will be built in city %s.",
           pcity->name);
       choice->want = 16000; /* diplomat more important than soldiers */
       pcity->ai.urgency = 1;
       choice->type = CT_DEFENDER;
       choice->choice = u;
    } else if (num_role_units(F_DIPLOMAT) > 0) {
      /* We don't know diplomats yet... */
      freelog(LOG_DIPLOMAT, "A defensive diplomat is wanted badly in city %s.",
           pcity->name);
      u = get_role_unit(F_DIPLOMAT, 0);
      /* 3000 is a just a large number, but not hillariously large as the
         previously used one. This is important for diplomacy later - Per */
      pplayer->ai.tech_want[get_unit_type(u)->tech_requirement] += 3000;
    }
  }
}

/**********************************************************************
  Calculates our need for diplomats as offensive units. May replace
  values in choice.
***********************************************************************/
void ai_choose_diplomat_offensive(struct player *pplayer,
                                  struct city *pcity,
                                  struct ai_choice *choice)
{
  Unit_Type_id u = best_role_unit(pcity, F_DIPLOMAT);

  if (u >= U_LAST) {
    /* We don't know diplomats yet! */
    return;
  }

  if (ai_handicap(pplayer, H_DIPLOMAT)) {
    /* Diplomats are too tough on newbies */
    return;
  }

  /* Do we have a good reason for building diplomats? */
  {
    struct unit_type *ut = get_unit_type(u);
    struct city *acity;
    int want, loss, p_success, p_failure, time_to_dest;
    int gain_incite = 0, gain_theft = 0, gain = 1;
    int incite_cost;
    struct unit *punit = create_unit_virtual(pplayer, pcity, u, FALSE);

    find_city_to_diplomat(pplayer, punit, &acity, &time_to_dest);

    if (acity == NULL || acity->ai.already_considered_for_diplomat) {
      /* Found no target or city already considered */
      return;
    }
    incite_cost = city_incite_cost(pplayer, acity);
    if (pplayers_at_war(pplayer, city_owner(acity))
        && !city_got_building(acity, B_PALACE)
        && !government_has_flag(get_gov_pplayer(city_owner(acity)),
                                G_UNBRIBABLE)
        && (incite_cost <
            pplayer->economic.gold - pplayer->ai.est_upkeep)) {
      /* incite gain (FIXME: we should count wonders too but need to
         cache that somehow to avoid CPU hog -- Per) */
      gain_incite = acity->food_prod * FOOD_WEIGHTING
                    + acity->shield_prod * SHIELD_WEIGHTING
                    + (acity->luxury_total
                       + acity->tax_total
                       + acity->science_total) * TRADE_WEIGHTING;
      gain_incite *= SHIELD_WEIGHTING; /* WAG cost to take city otherwise */
      gain_incite -= incite_cost;
    }
    if (city_owner(acity)->research.techs_researched <
             pplayer->research.techs_researched
             && !pplayers_allied(pplayer, city_owner(acity))) {
      /* tech theft gain */
      gain_theft = total_bulbs_required(pplayer) * TRADE_WEIGHTING;
    }
    gain = MAX(gain_incite, gain_theft);
    loss = ut->build_cost * SHIELD_WEIGHTING;

    /* Probability to succeed, assuming no defending diplomat */
    p_success = game.diplchance;
    /* Probability to lose our unit */
    p_failure = (unit_type_flag(u, F_SPY) ? 100 - p_success : 100);

    time_to_dest /= ut->move_rate;
    time_to_dest *= (time_to_dest/2); /* No long treks, please */

    /* Almost kill_desire */
    want = (p_success * gain - p_failure * loss) / 100
           - SHIELD_WEIGHTING * time_to_dest;
    if (want <= 0) {
      return;
    }

    want = military_amortize(pplayer, pcity, want, time_to_dest, 
                             ut->build_cost);

    if (!player_has_embassy(pplayer, city_owner(acity))) {
        freelog(LOG_DIPLOMAT, "A diplomat desired in %s to establish an "
                          "embassy with %s in %s", pcity->name,
                          city_owner(acity)->name, acity->name);
        want = MAX(want, 99);
    }
    if (want > choice->want) {
      freelog(LOG_DIPLOMAT, 
              "%s,%s: %s is desired with want %d (was %d) to spy "
              "in %s (incite desire %d, tech theft desire %d)",
              pplayer->name, pcity->name, ut->name, want, choice->want,
              acity->name, gain_incite, gain_theft);
      choice->want = want; 
      choice->type = CT_NONMIL; /* so we don't build barracks for it */
      choice->choice = u;
      acity->ai.already_considered_for_diplomat = TRUE;
    }
    destroy_unit_virtual(punit);
  }
}

/**************************************************************************
  Check if something is on our receiving end for some nasty diplomat
  business! Note that punit may die or be moved during this function. We
  must be adjacent to target city.

  We try to make embassy first, and abort if we already have one and target
  is allied. Then we steal, incite, sabotage or poison the city, in that
  order of priority.
**************************************************************************/
static void ai_diplomat_city(struct unit *punit, struct city *ctarget)
{
  struct packet_diplomat_action packet;
  struct player *pplayer = unit_owner(punit);
  struct player *tplayer = city_owner(ctarget);
  int count_impr = count_sabotagable_improvements(ctarget);
  int count_tech = count_stealable_techs(pplayer, tplayer);
  int gold_avail = pplayer->economic.gold - 2 * pplayer->ai.est_upkeep;
  int incite_cost;

  assert(pplayer->ai.control);

  if (punit->moves_left == 0) {
    UNIT_LOG(LOG_ERROR, punit, "no moves left in ai_diplomat_city()!");
  }

  handle_unit_activity_request(punit, ACTIVITY_IDLE);
  packet.diplomat_id = punit->id;
  packet.target_id = ctarget->id;

#define T(my_act,my_val)                                            \
  if (diplomat_can_do_action(punit, my_act, ctarget->x,             \
                             ctarget->y)) {                         \
    packet.action_type = my_act;                                    \
    packet.value = my_val;                                          \
    freelog(LOG_DIPLOMAT, "Player %s's diplomat %d does " #my_act   \
            " on %s", pplayer->name, punit->id, ctarget->name);     \
    handle_diplomat_action(pplayer, &packet);                       \
    return;                                                         \
  }

  if (!punit->foul) T(DIPLOMAT_EMBASSY,0);
  if (pplayers_allied(pplayer, tplayer)) {
    return; /* Don't do the rest to allies */
  }

  if (count_tech > 0 
      && (ctarget->steal == 0 || unit_flag(punit, F_SPY))) {
    T(DIPLOMAT_STEAL,0);
  } else {
    UNIT_LOG(LOG_DIPLOMAT, punit, "We have already stolen from %s!",
             ctarget->name);
  }

  incite_cost = city_incite_cost(pplayer, ctarget);
  if (incite_cost <= gold_avail) {
    T(DIPLOMAT_INCITE,0);
  } else {
    UNIT_LOG(LOG_DIPLOMAT, punit, "%s too expensive!", ctarget->name);
  }

  if (!pplayers_at_war(pplayer, tplayer)) {
    return; /* The rest are casus belli */
  }
  if (count_impr > 0) T(DIPLOMAT_SABOTAGE, B_LAST+1);
  T(SPY_POISON, 0); /* absolutely last resort */
#undef T

  /* This can happen for a number of odd and esoteric reasons  */
  UNIT_LOG(LOG_DIPLOMAT, punit, "decides to stand idle outside "
           "enemy city %s!", ctarget->name);
  ai_unit_new_role(punit, AIUNIT_NONE, -1, -1);
}

/**************************************************************************
  Find a city to send diplomats against, or NULL if none available on
  this continent.  punit can be virtual.
**************************************************************************/
static void find_city_to_diplomat(struct player *pplayer, struct unit *punit,
                                  struct city **ctarget, int *move_dist)
{
  bool has_embassy;
  int incite_cost = 0; /* incite cost */
  bool dipldef; /* whether target is protected by diplomats */

  assert(punit != NULL);
  *ctarget = NULL;
  *move_dist = MAX(map.xsize, map.ysize);

  simple_unit_path_iterator(punit, pos) {
    struct city *acity = map_get_city(pos.x, pos.y);
    struct player *aplayer;
    bool can_incite;

    if (!acity) {
      continue;
    }
    aplayer = city_owner(acity);

    /* sneaky way of avoiding foul diplomat capture  -AJS */
    has_embassy = player_has_embassy(pplayer, aplayer) || punit->foul;

    if (aplayer == pplayer || is_barbarian(aplayer)
        || (pplayers_allied(pplayer, aplayer) && has_embassy)) {
      continue; 
    }

    can_incite = !(city_got_building(acity, B_PALACE)
                   || government_has_flag(get_gov_pplayer(aplayer), 
                                          G_UNBRIBABLE));

    incite_cost = city_incite_cost(pplayer, acity);
    dipldef = (count_diplomats_on_tile(acity->x, acity->y) > 0);
    /* Three actions to consider:
     * 1. establishing embassy OR
     * 2. stealing techs OR
     * 3. inciting revolt */
    if ((!*ctarget || *move_dist > pos.total_MC)
        && (!has_embassy
            || (acity->steal == 0 && pplayer->research.techs_researched < 
                city_owner(acity)->research.techs_researched && !dipldef)
            || (incite_cost < (pplayer->economic.gold - pplayer->ai.est_upkeep)
                && can_incite && !dipldef))) {
      /* We have the closest enemy city so far on the same continent */
      *ctarget = acity;
      *move_dist = pos.total_MC;
    }
  } simple_unit_path_iterator_end;
}

/**************************************************************************
  Go to nearest/most threatened city (can be the current city too).
**************************************************************************/
static struct city *ai_diplomat_defend(struct player *pplayer,
                                       struct unit *punit,
                                       Unit_Type_id utype)
{
  int best_dist = 30; /* any city closer than this is better than none */
  int best_urgency = 0;
  struct city *ctarget = NULL;
  struct city *pcity = map_get_city(punit->x, punit->y);

  if (pcity 
      && count_diplomats_on_tile(pcity->x, pcity->y) == 1
      && pcity->ai.urgency > 0) {
    /* Danger and we are only diplomat present - stay. */
    return pcity;
  }

  simple_unit_path_iterator(punit, pos) {
    struct city *acity = map_get_city(pos.x, pos.y);
    struct player *aplayer;
    int dipls, urgency;

    if (!acity) {
      continue;
    }
    aplayer = city_owner(acity);
    if (aplayer != pplayer) {
      continue;
    }

    urgency = acity->ai.urgency;
    dipls = (count_diplomats_on_tile(pos.x, pos.y)
             - (same_pos(pos.x, pos.y, punit->x, punit->y) ? 1 : 0));
    if (dipls == 0 && acity->ai.diplomat_threat) {
      /* We are _really_ needed there */
      urgency = (urgency + 1) * 5;
    } else if (dipls > 0) {
      /* We are probably not needed there... */
      urgency /= 3;
    }

    /* This formula may not be optimal, but it works. */
    if (pos.total_MC > best_dist) {
      /* punish city for being so far away */
      urgency /= (float)(pos.total_MC / best_dist);
    }

    if (urgency > best_urgency) {
      /* Found something worthy of our presence */
      ctarget = acity;
      best_urgency = urgency;
      /* squelch divide-by-zero */
      best_dist = MAX(pos.total_MC, 1);
    }
  } simple_unit_path_iterator_end;

  return ctarget;
}

/**************************************************************************
  Find units that we can reach, and bribe them. Returns TRUE if survived
  the ordeal, FALSE if not or we expended all our movement.
  Will try to bribe a ship on the coast as well as land stuff.
**************************************************************************/
static bool ai_diplomat_bribe_nearby(struct player *pplayer, 
                                     struct unit *punit)
{
  struct packet_diplomat_action packet;
  int gold_avail = pplayer->economic.gold - pplayer->ai.est_upkeep;

  simple_unit_overlap_path_iterator(punit, pos) {
    struct tile *ptile = map_get_tile(pos.x, pos.y);
    bool threat = FALSE;
    int newval, bestval = 0, cost;
    struct unit *pvictim = unit_list_get(&ptile->units, 0);
    int sanity = punit->id;

    if (pos.total_MC > punit->moves_left) {
      /* Didn't find anything within range. */
      break;
    }

    if (!pvictim
        || !pplayers_at_war(pplayer, unit_owner(pvictim))
        || unit_list_size(&ptile->units) > 1
        || map_get_city(pos.x, pos.y)
        || government_has_flag(get_gov_pplayer(unit_owner(pvictim)),
                               G_UNBRIBABLE)) {
      continue;
    }

    /* Calculate if enemy is a threat */
    /* First find best defender on our tile */
    unit_list_iterate(ptile->units, aunit) {
      newval = DEFENCE_POWER(aunit);
      if (bestval < newval) {
        bestval = newval;
      }
    } unit_list_iterate_end;
    /* Compare with victim's attack power */
    newval = ATTACK_POWER(pvictim);
    if (newval > bestval
        && unit_type(pvictim)->move_rate > pos.total_MC) {
      /* Enemy can probably kill us */
      threat = TRUE;
    } else {
      /* Enemy cannot reach us or probably not kill us */
      threat = FALSE;
    }
    /* Don't bribe settlers! */
    if (unit_flag(pvictim, F_SETTLERS)
        || unit_flag(pvictim, F_CITIES)) {
      continue;
    }
    /* Should we make the expense? */
    cost = pvictim->bribe_cost = unit_bribe_cost(pvictim);
    if (!threat) {
      /* Don't empty our treasure without good reason! */
      gold_avail = pplayer->economic.gold - ai_gold_reserve(pplayer);
    }
    if (cost > gold_avail) {
      /* Can't afford */
      continue;
    }

    /* Found someone! */
    {
      int x, y;

      MAPSTEP(x, y, pos.x, pos.y, DIR_REVERSE(pos.dir_to_here));

      if (!ai_unit_goto(punit, x, y) || punit->moves_left <= 0) {
        return FALSE;
      }
    }

    if (diplomat_can_do_action(punit, DIPLOMAT_BRIBE, pos.x, pos.y)) {
      packet.diplomat_id = punit->id;
      packet.target_id = unit_list_get(&ptile->units, 0)->id;
      packet.action_type = DIPLOMAT_BRIBE;
      handle_diplomat_action(pplayer, &packet);
      /* autoattack might kill us as we move in */
      if (find_unit_by_id(sanity) && punit->moves_left > 0) {
        return TRUE;
      } else {
        return FALSE;
      }
    } else {
      /* usually because we ended move early due to another unit */
      UNIT_LOG(LOG_DIPLOMAT, punit, "could not bribe target (%d, %d), "
               " %d moves left", pos.x, pos.y, punit->moves_left);
      return FALSE;
    }
  } simple_unit_overlap_path_iterator_end;

  return (punit->moves_left > 0);
}

/**************************************************************************
  If we are the only diplomat in a threatened city, defend against enemy 
  actions. The passive defense is set by game.diplchance.  The active 
  defense is to bribe units which end their move nearby. Our next trick is 
  to look for enemy cities on our continent and do our diplomat things.

  FIXME: It is important to establish contact with all civilizations, so
  we should send diplomats by boat eventually. I just don't know how that
  part of the code works, yet - Per
**************************************************************************/
void ai_manage_diplomat(struct player *pplayer, struct unit *punit)
{
  struct city *pcity, *ctarget = NULL;

  CHECK_UNIT(punit);

  pcity = map_get_city(punit->x, punit->y);

  /* Look for someone to bribe */
  if (!ai_diplomat_bribe_nearby(pplayer, punit)) {
    /* Died or ran out of moves */
    return;
  }

  /* If we are the only diplomat in a threatened city, then stay to defend */
  pcity = map_get_city(punit->x, punit->y); /* we may have moved */
  if (pcity && count_diplomats_on_tile(punit->x, punit->y) == 1
      && pcity->ai.diplomat_threat) {
    UNIT_LOG(LOG_DIPLOMAT, punit, "stays to protect %s (urg %d)", 
             pcity->name, pcity->ai.urgency);
    ai_unit_new_role(punit, AIUNIT_NONE, -1, -1); /* abort mission */
    return;
  }

  /* If we have a role, we have a valid goto. Check if we have a valid city. */
  if (punit->ai.ai_role == AIUNIT_ATTACK
      || punit->ai.ai_role == AIUNIT_DEFEND_HOME) {
    ctarget = map_get_city(punit->goto_dest_x, punit->goto_dest_y);
  }

  /* Check if existing target still makes sense */
  if (punit->ai.ai_role == AIUNIT_ATTACK
      || punit->ai.ai_role == AIUNIT_DEFEND_HOME) {
    bool failure = FALSE;
    if (ctarget) {
      if (same_pos(ctarget->x, ctarget->y, punit->x, punit->y)) {
        failure = TRUE;
      } else if (pplayers_allied(pplayer, city_owner(ctarget))
          && punit->ai.ai_role == AIUNIT_ATTACK
          && player_has_embassy(pplayer, city_owner(ctarget))) {
        /* We probably incited this city with another diplomat */
        failure = TRUE;
      } else if (!pplayers_allied(pplayer, city_owner(ctarget))
                 && punit->ai.ai_role == AIUNIT_DEFEND_HOME) {
        /* We probably lost the city */
        failure = TRUE;
      }
    } else {
      /* City vanished! */
      failure = TRUE;
    }
    if (failure) {
      ai_unit_new_role(punit, AIUNIT_NONE, -1, -1);
    }
  }

  /* If we are not busy, acquire a target. */
  if (punit->ai.ai_role == AIUNIT_NONE) {
    enum ai_unit_task task;
    int move_dist; /* dummy */

    find_city_to_diplomat(pplayer, punit, &ctarget, &move_dist);

    if (ctarget) {
      task = AIUNIT_ATTACK;
      punit->ai.bodyguard = -1; /* want one */
      UNIT_LOG(LOG_DIPLOMAT, punit, "going on attack");
    } else if ((ctarget = ai_diplomat_defend(pplayer, punit,
                                             punit->type)) != NULL) {
      task = AIUNIT_DEFEND_HOME;
      UNIT_LOG(LOG_DIPLOMAT, punit, "going defensive");
    } else if ((ctarget = find_closest_owned_city(pplayer, punit->x, punit->y, 
                                                  TRUE, NULL)) != NULL) {
      /* This should only happen if the entire continent was suddenly
       * conquered. So we head for closest coastal city and wait for someone
       * to code ferrying for diplomats, or hostile attacks from the sea. */
      task = AIUNIT_DEFEND_HOME;
      UNIT_LOG(LOG_DIPLOMAT, punit, "going idle");
    } else {
      UNIT_LOG(LOG_DIPLOMAT, punit, "could not find a job");
      return;
    }

    ai_unit_new_role(punit, task, ctarget->x, ctarget->y);
  }

  CHECK_UNIT(punit);
  assert(punit->moves_left > 0 && ctarget && punit->ai.ai_role != AIUNIT_NONE);

  /* GOTO unless we want to stay */
  if (!same_pos(punit->x, punit->y, ctarget->x, ctarget->y)) {
    if (!ai_unit_gothere(punit)) {
      return;
    } 
  }

  /* Have we run out of moves? */
  if (punit->moves_left <= 0) {
    return;
  }

  /* Check if we can do something with our destination now. */
  if (punit->ai.ai_role == AIUNIT_ATTACK) {
    int dist = real_map_distance(punit->x, punit->y,
                                 punit->goto_dest_x,
                                 punit->goto_dest_y);
    UNIT_LOG(LOG_DIPLOMAT, punit, "attack, dist %d to %s (%s goto)",
             dist, ctarget ? ctarget->name : "(none)", 
             punit->activity == ACTIVITY_GOTO ? "has" : "no");
    if (dist == 1) {
      /* Do our stuff */
      ai_unit_new_role(punit, AIUNIT_NONE, -1, -1);
      ai_diplomat_city(punit, ctarget);
    }
  }
}
