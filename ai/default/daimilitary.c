/***********************************************************************
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
#include <fc_config.h>
#endif

#include <string.h>

/* utility */
#include "log.h"

/* common */
#include "combat.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "movement.h"
#include "research.h"
#include "specialist.h"
#include "unitlist.h"

/* common/aicore */
#include "pf_tools.h"

/* server */
#include "citytools.h"
#include "cityturn.h"
#include "srv_log.h"
#include "srv_main.h"

/* server/advisors */
#include "advbuilding.h"
#include "advchoice.h"
#include "advdata.h"
#include "advgoto.h"
#include "advtools.h"
#include "infracache.h"

/* ai */
#include "aitraits.h"
#include "difficulty.h"
#include "handicaps.h"

/* ai/default */
#include "daiair.h"
#include "daicity.h"
#include "daidata.h"
#include "daidiplomat.h"
#include "daieffects.h"
#include "daiferry.h"
#include "daihunter.h"
#include "dailog.h"
#include "daiparadrop.h"
#include "daiplayer.h"
#include "daitech.h"
#include "daitools.h"

#include "daimilitary.h"

/* Size 1 city gets destroyed when conquered. It's still a good thing
 * stop enemy from having it. */
#define CITY_CONQUEST_WORTH(_city_, _data_) \
  (_data_->worth * 0.9 + (city_size_get(_city_) - 0.5) * 10)

static unsigned int assess_danger(struct ai_type *ait,
                                  const struct civ_map *nmap,
                                  struct city *pcity,
                                  player_unit_list_getter ul_cb);

static adv_want dai_unit_attack_desirability(struct ai_type *ait,
                                             const struct unit_type *punittype);
static adv_want dai_unit_defense_desirability(struct ai_type *ait,
                                              const struct unit_type *punittype);

/**********************************************************************//**
  Choose the best unit the city can build to defend against attacker v.
**************************************************************************/
struct unit_type *dai_choose_defender_versus(struct city *pcity,
                                             struct unit *attacker)
{
  struct unit_type *bestunit = NULL;
  double best = 0;
  int best_cost = FC_INFINITY;
  struct player *pplayer = city_owner(pcity);
  struct civ_map *nmap = &(wld.map);

  simple_ai_unit_type_iterate(punittype) {
    if (can_city_build_unit_now(nmap, pcity, punittype)) {
      int fpatt, fpdef, defense, attack;
      double want, loss, cost = utype_build_shield_cost(pcity, NULL, punittype);
      struct unit *defender;
      int veteran = get_unittype_bonus(city_owner(pcity), pcity->tile,
                                       punittype, NULL,
                                       EFT_VETERAN_BUILD);

      defender = unit_virtual_create(pplayer, pcity, punittype, veteran);
      defense = get_total_defense_power(attacker, defender);
      attack = get_total_attack_power(attacker, defender, NULL);
      get_modified_firepower(nmap, attacker, defender, &fpatt, &fpdef);

      /* Greg's algorithm. loss is the average number of health lost by
       * defender. If loss > attacker's hp then we should win the fight,
       * which is always a good thing, since we avoid shield loss. */
      loss = (double) defense * punittype->hp * fpdef / (attack * fpatt);
      want = (loss + MAX(0, loss - attacker->hp)) / cost;

#ifdef NEVER
      CITY_LOG(LOG_DEBUG, pcity, "desire for %s against %s(%d,%d) is %.2f",
               utype_rule_name(punittype),
               unit_rule_name(attacker),
               TILE_XY(attacker->tile), want);
#endif /* NEVER */

      if (want > best || (ADV_WANTS_EQ(want, best)
                          && cost <= best_cost)) {
        best = want;
        bestunit = punittype;
        best_cost = cost;
      }
      unit_virtual_destroy(defender);
    }
  } simple_ai_unit_type_iterate_end;

  return bestunit;
}

/**********************************************************************//**
  Choose best attacker based on movement type. It chooses based on unit
  desirability without regard to cost, unless costs are equal. This is
  very wrong. FIXME, use amortize on time to build.
**************************************************************************/
static struct unit_type *dai_choose_attacker(struct ai_type *ait,
                                             const struct civ_map *nmap,
                                             struct city *pcity,
                                             enum terrain_class tc,
                                             bool allow_gold_upkeep)
{
  struct unit_type *bestid = NULL;
  adv_want best = -1;
  adv_want cur;
  struct player *pplayer = city_owner(pcity);

  simple_ai_unit_type_iterate(putype) {
    if (!allow_gold_upkeep
        && utype_upkeep_cost(putype, nullptr, pplayer, O_GOLD) > 0) {
      continue;
    }

    cur = dai_unit_attack_desirability(ait, putype);
    if ((tc == TC_LAND && utype_class(putype)->adv.land_move != MOVE_NONE)
        || (tc == TC_OCEAN
            && utype_class(putype)->adv.sea_move != MOVE_NONE)) {
      if (can_city_build_unit_now(nmap, pcity, putype)
          && (cur > best
              || (ADV_WANTS_EQ(cur, best)
                  && utype_build_shield_cost(pcity, NULL, putype)
                    <= utype_build_shield_cost(pcity, NULL, bestid)))) {
        best = cur;
        bestid = putype;
      }
    }
  } simple_ai_unit_type_iterate_end;

  return bestid;
}

/**********************************************************************//**
  Choose best defender based on movement type. It chooses based on unit
  desirability without regard to cost, unless costs are equal. This is
  very wrong. FIXME, use amortize on time to build.

  We should only be passed with L_DEFEND_GOOD role for now, since this
  is the only role being considered worthy of bodyguarding in findjob.
**************************************************************************/
static struct unit_type *dai_choose_bodyguard(struct ai_type *ait,
                                              const struct civ_map *nmap,
                                              struct city *pcity,
                                              enum terrain_class tc,
                                              enum unit_role_id role,
                                              bool allow_gold_upkeep)
{
  struct unit_type *bestid = NULL;
  adv_want best = 0;
  struct player *pplayer = city_owner(pcity);

  simple_ai_unit_type_iterate(putype) {
    /* Only consider units of given role, or any if invalid given */
    if (unit_role_id_is_valid(role)) {
      if (!utype_has_role(putype, role)) {
        continue;
      }
    }

    if (!allow_gold_upkeep
        && utype_upkeep_cost(putype, nullptr, pplayer, O_GOLD) > 0) {
      continue;
    }

    /* Only consider units of same move type */
    if ((tc == TC_LAND && utype_class(putype)->adv.land_move == MOVE_NONE)
        || (tc == TC_OCEAN
            && utype_class(putype)->adv.sea_move == MOVE_NONE)) {
      continue;
    }

    /* Now find best */
    if (can_city_build_unit_now(nmap, pcity, putype)) {
      const adv_want desire = dai_unit_defense_desirability(ait, putype);

      if (desire > best
          || (ADV_WANTS_EQ(desire, best) && utype_build_shield_cost(pcity, NULL, putype) <=
              utype_build_shield_cost(pcity, NULL, bestid))) {
        best = desire;
        bestid = putype;
      }
    }
  } simple_ai_unit_type_iterate_end;

  return bestid;
}

/**********************************************************************//**
  Helper for assess_defense_quadratic() and assess_defense_unit().
**************************************************************************/
static int base_assess_defense_unit(struct city *pcity, struct unit *punit,
                                    bool igwall, bool quadratic,
                                    int wall_value)
{
  int defense;
  int fp;

  if (is_special_unit(punit)) {
    return 0;
  }

  defense = get_fortified_defense_power(NULL, punit) * punit->hp;
  fp = unit_type_get(punit)->firepower;
  if (unit_has_type_flag(punit, UTYF_BADCITYDEFENDER)) {
    /* Attacker firepower doubled, defender firepower set to
     * game.info.low_firepower_pearl_harbor at max. */
    defense *= MIN(fp, game.info.low_firepower_pearl_harbor);
    defense /= 2;
  } else {
    defense *= fp;
  }

  defense /= POWER_DIVIDER;

  if (quadratic) {
    defense *= defense;
  }

  if (pcity != NULL && !igwall && city_got_defense_effect(pcity, NULL)) {
    /* FIXME: We checked if city got defense effect against *some*
     * unit type. Sea unit danger might cause us to build defenses
     * against air units... */

    /* TODO: What about wall_value < 10? Do we really want walls to
     *       result in decrease in the returned value? */
    defense *= wall_value;
    defense /= 10;
  }

  return defense;
}

/**********************************************************************//**
  Need positive feedback in m_a_c_b and bodyguard routines. -- Syela
**************************************************************************/
int assess_defense_quadratic(struct ai_type *ait, struct city *pcity)
{
  int defense = 0, walls = 0;
  /* This can be an arg if needed, but we don't need to change it now. */
  const bool igwall = FALSE;
  struct ai_city *city_data = def_ai_city_data(pcity, ait);

  /* wallvalue = 10, walls = 10,
   * wallvalue = 40, walls = 20,
   * wallvalue = 90, walls = 30 */

  while (walls * walls < city_data->wallvalue * 10) {
    walls++;
  }

  unit_list_iterate(pcity->tile->units, punit) {
    defense += base_assess_defense_unit(pcity, punit, igwall, FALSE,
                                        walls);
  } unit_list_iterate_end;

  if (defense > 1<<12) {
    CITY_LOG(LOG_VERBOSE, pcity, "Overflow danger in assess_defense_quadratic:"
             " %d", defense);
    if (defense > 1<<15) {
      defense = 1<<15; /* more defense than we know what to do with! */
    }
  }

  return defense * defense;
}

/**********************************************************************//**
  One unit only, mostly for findjob; handling boats correctly. 980803 -- Syela
**************************************************************************/
int assess_defense_unit(struct ai_type *ait, struct city *pcity,
                        struct unit *punit, bool igwall)
{
  return base_assess_defense_unit(pcity, punit, igwall, TRUE,
                                  def_ai_city_data(pcity, ait)->wallvalue);
}

/**********************************************************************//**
  Most of the time we don't need/want positive feedback. -- Syela

  It's unclear whether this should treat settlers/caravans as defense. -- Syela
  TODO: It looks like this is never used while deciding if we should attack
  pcity, if we have pcity defended properly, so I think it should. --pasky
**************************************************************************/
static int assess_defense_backend(struct ai_type *ait, struct city *pcity,
                                  bool igwall)
{
  /* Estimate of our total city defensive might */
  int defense = 0;

  unit_list_iterate(pcity->tile->units, punit) {
    defense += assess_defense_unit(ait, pcity, punit, igwall);
  } unit_list_iterate_end;

  return defense;
}

/**********************************************************************//**
  Estimate defense strength of city
**************************************************************************/
int assess_defense(struct ai_type *ait, struct city *pcity)
{
  return assess_defense_backend(ait, pcity, FALSE);
}

/**********************************************************************//**
  Estimate defense strength of city without considering how buildings
  help defense
**************************************************************************/
static int assess_defense_igwall(struct ai_type *ait, struct city *pcity)
{
  return assess_defense_backend(ait, pcity, TRUE);
}

/**********************************************************************//**
  While assuming an attack on a target in few turns, tries to guess
  if a requirement req will be fulfilled for this target
  n_data for a number of turns after which the strike is awaited
  (0 means current turn, assumes a strike during 5 turns)
**************************************************************************/
static enum fc_tristate
tactical_req_cb(const struct req_context *context,
                const struct req_context *other_context,
                const struct requirement *req,
                void *data, int n_data)
{
  switch (req->source.kind) {
  case VUT_IMPROVEMENT:
  case VUT_SITE:
    {
      const struct impr_type *b = req->source.value.building;

      /* FIXME: in actor_reqs, may allow attack _from_ a city with... */
      if (req->survives || NULL == context->city || is_great_wonder(b)
          || !city_has_building(context->city, b) || b->sabotage <= 0) {
        return tri_req_active(context, other_context, req);
      }
      /* Else may be sabotaged */
    }
    fc__fallthrough;
  case VUT_UNITSTATE:
  case VUT_ACTIVITY:
  case VUT_MINSIZE:
  case VUT_MAXTILETOTALUNITS:
  case VUT_MAXTILETOPUNITS:
  case VUT_MINHP:
  case VUT_MINMOVES:
  case VUT_COUNTER:
    /* Can be changed back or forth quickly */
    return TRI_MAYBE;
  case VUT_MINVETERAN:
    /* Can be changed forth but not back */
    return is_req_preventing(context, other_context, req, RPT_POSSIBLE)
           > REQUCH_CTRL ? TRI_NO : TRI_MAYBE;
  case VUT_MINFOREIGNPCT:
  case VUT_NATIONALITY:
    /* Can be changed back but hardly forth (foreign citizens reduced first) */
    switch (tri_req_active(context, other_context, req)) {
    case TRI_NO:
      return req->present ? TRI_NO : TRI_MAYBE;
    case TRI_YES:
      return req->present ? TRI_MAYBE : TRI_YES;
    default:
      return TRI_MAYBE;
    }
  case VUT_DIPLREL:
  case VUT_DIPLREL_TILE:
  case VUT_DIPLREL_TILE_O:
  case VUT_DIPLREL_UNITANY:
  case VUT_DIPLREL_UNITANY_O:
    /* If the attack happens, there is a diplrel that allows it */
    return TRI_YES;
  case VUT_AGE:
  case VUT_FORM_AGE:
  case VUT_MINCALFRAG:
  case VUT_MINYEAR:
    /* If it is not near, won't change */
    return tri_req_active_turns(n_data, 5 /* WAG */,
                                context, other_context, req);
  case VUT_CITYSTATUS:
    if (CITYS_OWNED_BY_ORIGINAL != req->source.value.citystatus
        && CITYS_TRANSFERRED != req->source.value.citystatus) {
      return TRI_MAYBE;
    }
    fc__fallthrough;
  case VUT_UTYPE:
  case VUT_UTFLAG:
  case VUT_UCLASS:
  case VUT_UCFLAG:
    /* FIXME: Support converting siege machines (needs hard reqs checked) */
  case VUT_ACTION:
  case VUT_OTYPE:
  case VUT_SPECIALIST:
  case VUT_EXTRAFLAG:
  case VUT_MINLATITUDE:
  case VUT_MAXLATITUDE:
  case VUT_AI_LEVEL:
  case VUT_CITYTILE:
  case VUT_STYLE:
  case VUT_TOPO:
  case VUT_WRAP:
  case VUT_SERVERSETTING:
  case VUT_NATION:
  case VUT_NATIONGROUP:
  case VUT_ADVANCE:
  case VUT_TECHFLAG:
  case VUT_GOVERNMENT:
  case VUT_GOVFLAG:
  case VUT_ACHIEVEMENT:
  case VUT_IMPR_GENUS:
  case VUT_IMPR_FLAG:
  case VUT_PLAYER_FLAG:
  case VUT_PLAYER_STATE:
  case VUT_MINCULTURE:
  case VUT_MINTECHS:
  case VUT_FUTURETECHS:
  case VUT_MINCITIES:
  case VUT_ORIGINAL_OWNER:
  case VUT_ROADFLAG:
  case VUT_TERRAIN:
  case VUT_EXTRA:
  case VUT_TILEDEF:
  case VUT_GOOD:
  case VUT_TERRAINCLASS:
  case VUT_TERRFLAG:
  case VUT_TERRAINALTER:
  case VUT_MAX_DISTANCE_SQ:
  case VUT_MAX_REGION_TILES:
  case VUT_TILE_REL:
  case VUT_NONE:
    return tri_req_active(context, other_context, req);
  case VUT_COUNT:
    /* Not implemented. */
    break;
  }
  fc_assert(FALSE);

  return TRI_NO;
}

/**********************************************************************//**
  See if there is an enabler for particular action to be targeted at
  pcity's tile after turns (for 5 turns more).
  Note that hard reqs are not tested except for utype
  and that the actor player is ignored; also, it's not tested can utype
  attack the city's particular terrain.
**************************************************************************/
static bool
action_may_happen_unit_on_city(const action_id wanted_action,
                               const struct unit *actor,
                               const struct city *pcity,
                               int turns)
{
  const struct player *target_player = city_owner(pcity),
      *actor_player = unit_owner(actor);
  const struct unit_type *utype = unit_type_get(actor);
  const struct req_context target_ctx = {
      .player = target_player,
      .city = pcity,
      .tile = city_tile(pcity)
  }, actor_ctx = {
      .player = actor_player,
      .unit = actor,
      .unittype = utype
  };

  if (!utype_can_do_action(utype, wanted_action)) {
    return FALSE;
  }
  action_enabler_list_iterate(action_enablers_for_action(wanted_action),
                              enabler) {
    /* We assume that we could build or move units into the city
     * that are not present there yet */
    if (TRI_NO != tri_reqs_cb_active(&actor_ctx, &target_ctx,
                                     &enabler->actor_reqs, NULL,
                                     tactical_req_cb, NULL, turns)
        &&
        TRI_NO != tri_reqs_cb_active(&target_ctx, &actor_ctx,
                                     &enabler->target_reqs, NULL,
                                     tactical_req_cb, NULL, turns)) {
      return TRUE;
    }
  } action_enabler_list_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  How dangerous and far a unit is for a city?
**************************************************************************/
static unsigned int assess_danger_unit(const struct civ_map *nmap,
                                       const struct city *pcity,
                                       struct pf_reverse_map *pcity_map,
                                       const struct unit *punit,
                                       int *move_time)
{
  struct pf_position pos;
  const struct unit_type *punittype = unit_type_get(punit);
  const struct tile *ptile = city_tile(pcity);
  const struct player *uowner = unit_owner(punit);
  const struct unit *ferry;
  unsigned int danger;
  int amod = -99, dmod;
  bool attack_danger = FALSE;

  *move_time = PF_IMPOSSIBLE_MC;

  if ((utype_can_do_action_result(punittype, ACTRES_PARADROP)
       || utype_can_do_action_result(punittype, ACTRES_PARADROP_CONQUER))
      && 0 < punittype->paratroopers_range) {
    *move_time = (real_map_distance(ptile, unit_tile(punit))
                  / punittype->paratroopers_range);
  }

  if (pf_reverse_map_unit_position(pcity_map, punit, &pos)
      && (PF_IMPOSSIBLE_MC == *move_time
          || *move_time > pos.turn)) {
    *move_time = pos.turn;
  }

  if (unit_transported(punit)
      && (ferry = unit_transport_get(punit))
      && pf_reverse_map_unit_position(pcity_map, ferry, &pos)) {
    if ((PF_IMPOSSIBLE_MC == *move_time
         || *move_time > pos.turn)) {
      *move_time = pos.turn;
      if (!can_attack_from_non_native(punittype)) {
        (*move_time)++;
      }
    }
  }

  if (PF_IMPOSSIBLE_MC == *move_time) {
    return 0;
  }
  if (!is_native_tile(punittype, ptile)
      && !can_attack_non_native(punittype)) {
    return 0;
  }
  if (!is_native_near_tile(nmap, unit_class_get(punit), ptile)) {
    return 0;
  }

  /* Find the worst attack action to expect */
  action_by_result_iterate(paction, ACTRES_ATTACK) {
    /* Is it possible that punit will do action id to the city? */
    /* FIXME: some unit parameters (notably, veterancy) may meddle in */

    if (action_may_happen_unit_on_city(action_id(paction), punit, pcity, *move_time)) {
      int b;

      attack_danger = TRUE;

      b = get_unittype_bonus(uowner, ptile, punittype, paction, EFT_ATTACK_BONUS);
      if (b > amod) {
        amod = b;
      }
    }
  } action_by_result_iterate_end;

  /* FIXME: it's a dummy support for anti-bombard defense just to do something against
   * approaching bombarders. Some better logic is needed, see OSDN#41778 */
  if (!attack_danger) {
    action_by_result_iterate(paction, ACTRES_BOMBARD) {
      /* FIXME: some unit parameters (notably, veterancy) may meddle in */

      if (action_may_happen_unit_on_city(action_id(paction), punit, pcity,
                                         *move_time)) {
        int b;

        attack_danger = TRUE;

        b = get_unittype_bonus(uowner, ptile, punittype, paction, EFT_ATTACK_BONUS);
        if (b > amod) {
          amod = b;
        }
      }
    } action_by_result_iterate_end;
    /* Here something should be done cuz the modifier affects
     * more than one unit but not their full hp, but is not done yet...*/
  }
  if (!attack_danger) {
    /* If the unit is dangerous, it's not about its combat strength */
    return 0;
  }

  danger = adv_unit_att_rating(punit);
  dmod = 100 + get_unittype_bonus(city_owner(pcity), ptile,
                                  punittype, NULL, EFT_DEFEND_BONUS);
  return danger * (amod + 100) / MAX(dmod, 1);
}

/**********************************************************************//**
  Call assess_danger() for all cities owned by pplayer.

  This is necessary to initialize some ai data before some ai calculations.
**************************************************************************/
void dai_assess_danger_player(struct ai_type *ait,
                              const struct civ_map *nmap,
                              struct player *pplayer)
{
  /* Do nothing if game is not running */
  if (S_S_RUNNING == server_state()) {
    city_list_iterate(pplayer->cities, pcity) {
      (void) assess_danger(ait, nmap, pcity, NULL);
    } city_list_iterate_end;
  }
}

/**********************************************************************//**
  Set (overwrite) our want for a building. Syela tries to explain:

    My first attempt to allow ng_wa >= 200 led to stupidity in cities
    with no defenders and danger = 0 but danger > 0. Capping ng_wa at
    100 + urgency led to a failure to buy walls as required.  Allowing
    want > 100 with !urgency led to the AI spending too much gold and
    falling behind on science.  I'm trying again, but this will require
    yet more tedious observation -- Syela

  The idea in this horrible function is that there is an enemy nearby
  that can whack us, so let's build something that can defend against
  them. If danger is urgent and overwhelming, danger is 200+, if it is
  only overwhelming, set it depending on danger. If it is underwhelming,
  set it to 100 plus urgency.

  This algorithm is very strange. But I created it by nesting up
  Syela's convoluted if ... else logic, and it seems to work. -- Per
**************************************************************************/
static void dai_reevaluate_building(struct city *pcity, adv_want *value,
                                    unsigned int urgency, unsigned int danger,
                                    int defense)
{
  if (*value == 0 || danger <= 0) {
    return;
  }

  *value = MAX(*value, 100 + urgency); /* default */

  if (urgency > 0 && danger > defense * 2) {
    *value += 100;
  } else if (defense != 0 && danger > defense) {
    *value = MAX(danger * 100 / defense, *value);
  }
}

/**********************************************************************//**
  Create cached information about danger, urgency and grave danger to our
  cities.

  Danger is a weight on how much power enemy units nearby have, which is
  compared to our defense.

  Urgency is the number of hostile units that can attack us in three turns.

  Grave danger is number of units that can attack us next turn.

  FIXME: We do not consider a paratrooper's mr_req and mr_sub
  fields. Not a big deal, though.

  FIXME: Due to the nature of assess_distance, a city will only be
  afraid of a boat laden with enemies if it stands on the coast (i.e.
  is directly reachable by this boat).
**************************************************************************/
static unsigned int assess_danger(struct ai_type *ait,
                                  const struct civ_map *nmap,
                                  struct city *pcity,
                                  player_unit_list_getter ul_cb)
{
  struct player *pplayer = city_owner(pcity);
  struct tile *ptile = city_tile(pcity);
  struct ai_city *city_data = def_ai_city_data(pcity, ait);
  /* How much such danger there is that building would help against. */
  unsigned int danger_reduced[B_LAST];
  int i;
  int defender;
  unsigned int urgency = 0;
  int defense;
  int total_danger = 0;

  /* TODO: Presumably most, or even all, of these arrays
   *       could be of size game.control.num_unit_types instead
   *       of full U_LAST */
  int defense_bonuses_pct[U_LAST];
  bool defender_type_handled[U_LAST];
  int best_non_scramble[U_LAST];
  bool sth_does_not_scramble = FALSE;
  int city_def_against[U_LAST];
  int assess_turns;
  bool omnimap;

  TIMING_LOG(AIT_DANGER, TIMER_START);

  /* Initialize data. */
  memset(&danger_reduced, 0, sizeof(danger_reduced));
  if (has_handicap(pplayer, H_DANGER)) {
    /* Always thinks that city is in grave danger */
    city_data->grave_danger = 1;
  } else {
    city_data->grave_danger = 0;
  }
  city_data->diplomat_threat = FALSE;
  city_data->has_diplomat = FALSE;

  unit_type_iterate(utype) {
    int idx = utype_index(utype);

    defense_bonuses_pct[idx] = 0;
    defender_type_handled[idx] = FALSE;
    best_non_scramble[idx] = -1;
    /* FIXME: cache it somewhere? */
    city_def_against[idx]
      = 100 + get_unittype_bonus(pplayer, ptile, utype, NULL,
                                 EFT_DEFEND_BONUS);
    city_def_against[idx] = MAX(city_def_against[idx], 1);
  } unit_type_iterate_end;

  /* What flag-specific bonuses do our units have. */
  /* We value them less than general defense increment */
  unit_list_iterate(ptile->units, punit) {
    const struct unit_type *def = unit_type_get(punit);

    if (unit_has_type_flag(punit, UTYF_DIPLOMAT)) {
      city_data->has_diplomat = TRUE;
    }
    if (!defender_type_handled[utype_index(def)]) {
      /* This is first defender of this type. Check defender type
       * specific bonuses. */

      /* Skip defenders that have no bonuses at all. Acceptable
       * side-effect is that we can't consider negative bonuses at
       * all ("No bonuses" should be better than "negative bonus") */
      if (def->cache.max_defense_mp_bonus_pct > 0) {
        unit_type_iterate(utype) {
          int idx = utype_index(utype);
          int coeff = def->cache.scramble_coeff[idx];
          int bonus;

          /* FIXME: consider EFT_FORTIFY_DEFENSE_BONUS */
          if (coeff) {
            bonus = coeff / city_def_against[idx] - 100;
          } else {
            bonus = def->cache.defense_mp_bonuses_pct[idx];
          }

          if (bonus > defense_bonuses_pct[idx]) {
            if (!coeff) {
              best_non_scramble[idx] = bonus;
            }
            defense_bonuses_pct[idx] = bonus;
          } else if (!coeff) {
            best_non_scramble[idx] = MAX(best_non_scramble[idx], bonus);
          }
        } unit_type_iterate_end;
      } else {
        /* Just remember that such a unit exists and hope its bonuses are just 0 */
        sth_does_not_scramble = TRUE;
      }

      defender_type_handled[utype_index(def)] = TRUE;
    }
  } unit_list_iterate_end;
  if (sth_does_not_scramble || unit_list_size(ptile->units) <= 0) {
    /* Scrambling units tend to be expensive. If we have a barenaked city, we'll
     * need at least a cheap unit. If we have only scramblers,
     * maybe it's OK. */
    unit_type_iterate(scrambler_type) {
      Unit_type_id id = utype_index(scrambler_type);

      best_non_scramble[id] = MAX(best_non_scramble[id], 0);
    } unit_type_iterate_end;
  }

  if (player_is_cpuhog(pplayer)) {
    assess_turns = 6;
  } else {
    assess_turns = has_handicap(pplayer, H_ASSESS_DANGER_LIMITED) ? 2 : 3;
  }

  omnimap = !has_handicap(pplayer, H_MAP);

  /* Check. */
  players_iterate(aplayer) {
    struct pf_reverse_map *pcity_map;
    struct unit_list *units;

    if (!adv_is_player_dangerous(pplayer, aplayer)) {
      continue;
    }
    /* Note that we still consider the units of players we are not (yet)
     * at war with. */

    pcity_map = pf_reverse_map_new_for_city(nmap, pcity, aplayer, assess_turns,
                                            omnimap);

    if (ul_cb != NULL) {
      units = ul_cb(aplayer);
    } else {
      units = aplayer->units;
    }
    unit_list_iterate(units, punit) {
      int move_time;
      unsigned int vulnerability;
      int defbonus_pct;
      const struct unit_type *utype = unit_type_get(punit);
      struct unit_type_ai *utai = utype_ai_data(utype, ait);

#ifdef FREECIV_WEB
      /* freeciv-web ignores danger that is far in distance,
       * no matter how quickly it would reach us; even if
       * that's *immediately* over a road type that allows
       * unlimited movement. */
      int unit_distance = real_map_distance(ptile, unit_tile(punit));

      if (unit_distance > ASSESS_DANGER_MAX_DISTANCE
          || (has_handicap(pplayer, H_ASSESS_DANGER_LIMITED)
              && unit_distance > AI_HANDICAP_DISTANCE_LIMIT)) {
        /* Too far away. */
        continue;
      }
#endif /* FREECIV_WEB */

      if (!utai->carries_occupiers
          && !utype_acts_hostile(utype)) {
        /* Harmless unit. */
        continue;
      }

      /* Defender unspecific vulnerability and potential move time */
      vulnerability = assess_danger_unit(nmap, pcity, pcity_map,
                                         punit, &move_time);

      if (PF_IMPOSSIBLE_MC == move_time) {
        continue;
      }

      if (unit_can_take_over(punit) || utai->carries_occupiers) {
        /* Even if there is no attack strength,
         * we need ANY protector for the city */
        vulnerability = MAX(vulnerability, 1);
        if (3 >= move_time) {
          urgency++;
          if (1 >= move_time) {
            city_data->grave_danger++;
          }
        }
      }

      defbonus_pct = defense_bonuses_pct[utype_index(utype)];
      if (defbonus_pct > 100) {
        defbonus_pct = (defbonus_pct + 100) / 2;
      }
      /* Reduce vulnerability for specific bonuses we do have */
      vulnerability = vulnerability * 100 / (defbonus_pct + 100);
      /* Pass the order for a new defender type against it
       * to the scientific advisor, urgency considered */
      (void) dai_wants_defender_against(ait, nmap, pplayer, pcity, utype,
                                        vulnerability / MAX(move_time, 1));

      if (utype_acts_hostile(unit_type_get(punit)) && 2 >= move_time) {
        city_data->diplomat_threat = TRUE;
      }

      vulnerability *= vulnerability; /* positive feedback */
      if (1 < move_time) {
        vulnerability /= move_time;
      }

      if (unit_can_do_action(punit, ACTION_NUKE)
          || unit_can_do_action(punit, ACTION_NUKE_CITY)
          || unit_can_do_action(punit, ACTION_NUKE_UNITS)) {
        defender = dai_find_source_building(pcity, EFT_NUKE_PROOF, utype);
        if (defender != B_LAST) {
          danger_reduced[defender] += vulnerability / MAX(move_time, 1);
        }
      } else if (best_non_scramble[utype_index(utype)] >= 0) {
        /* To consider building a defensive building,
         * build first a defender that gets any profit of it */
        defender = dai_find_source_building(pcity, EFT_DEFEND_BONUS, utype);
        if (defender != B_LAST) {
          int danred = vulnerability / MAX(move_time, 1);
          /* Maybe we can build an improvement
           * that gets normal defender over scrambling one?
           * Effectively a sqrt-scale because we don't test how high
           * the effect is. */
          if (best_non_scramble[utype_index(utype)]
              < defense_bonuses_pct[utype_index(utype)]) {
            danred = danred * (100 + best_non_scramble[utype_index(utype)])
                     / (100 + defense_bonuses_pct[utype_index(utype)]);
          }
          danger_reduced[defender] += danred;
        }
      }

      total_danger += vulnerability;
    } unit_list_iterate_end;

    pf_reverse_map_destroy(pcity_map);

  } players_iterate_end;

  if (total_danger) {
    /* If any hostile player has any dangerous unit that can in any time
     * reach the city, we consider building walls here, if none yet.
     * FIXME: this value assumes that walls give x3 defense. */
    city_data->wallvalue = 90;
  } else {
    /* No danger.
     * This is half of the wallvalue of what danger 1 would produce. */
    city_data->wallvalue = 5;
  }

  if (0 < city_data->grave_danger) {
    /* really, REALLY urgent to defend */
    urgency += 10 * city_data->grave_danger;
  }

  /* HACK: This needs changing if multiple improvements provide
   * this effect. */
  /* FIXME: Accept only buildings helping unit classes we actually use.
   *        Now we consider any land mover helper suitable. */
  /* Sum of squared defense ratings */
  defense = assess_defense_igwall(ait, pcity);

  for (i = 0; i < B_LAST; i++) {
    if (0 < danger_reduced[i]) {
      dai_reevaluate_building(pcity, &pcity->server.adv->building_want[i],
                              urgency, danger_reduced[i], defense);
    }
  }

  if (has_handicap(pplayer, H_DANGER) && 0 == total_danger) {
    /* Has to have some danger
     * Otherwise grave_danger will be ignored. */
    city_data->danger = 1;
  } else {
    city_data->danger = total_danger;
  }
  city_data->urgency = urgency;

  TIMING_LOG(AIT_DANGER, TIMER_STOP);

  return urgency;
}

/**********************************************************************//**
  How much we would want that unit to defend a city? (Do not use this
  function to find bodyguards for ships or air units.)
**************************************************************************/
static adv_want dai_unit_defense_desirability(struct ai_type *ait,
                                              const struct unit_type *punittype)
{
  adv_want desire = punittype->hp;
  int attack = punittype->attack_strength;
  int defense = punittype->defense_strength;
  int maxbonus_pct = 0;
  int fp = punittype->firepower;

  /* Sea and helicopters often have their firepower set to low firepower when
   * defending. We can't have such units as defenders. */
  if (utype_has_flag(punittype, UTYF_BADCITYDEFENDER)) {
    fp = MIN(fp, game.info.low_firepower_pearl_harbor);
  }
  if (((struct unit_type_ai *)utype_ai_data(punittype, ait))->low_firepower) {
    fp = MIN(fp, game.info.low_firepower_combat_bonus);
  }
  desire *= fp;
  desire *= defense;
  desire += punittype->move_rate / SINGLE_MOVE;
  desire += attack;

  maxbonus_pct = punittype->cache.max_defense_mp_bonus_pct;
  if (maxbonus_pct > 100) {
    maxbonus_pct = (maxbonus_pct + 100) / 2;
  }
  desire += desire * maxbonus_pct / 100;
  if (utype_has_flag(punittype, UTYF_GAMELOSS)) {
    desire /= 10; /* But might actually be worth it */
  }

  return desire;
}

/**********************************************************************//**
  How much we would want that unit to attack with?
**************************************************************************/
static adv_want dai_unit_attack_desirability(struct ai_type *ait,
                                             const struct unit_type *punittype)
{
  adv_want desire = punittype->hp;
  int attack = punittype->attack_strength;
  int defense = punittype->defense_strength;

  desire *= punittype->move_rate;
  desire *= punittype->firepower;
  desire *= attack;
  desire += defense;
  if (utype_has_flag(punittype, UTYF_IGTER)) {
    desire += desire / 2;
  }
  if (utype_has_flag(punittype, UTYF_GAMELOSS)) {
    desire /= 10; /* But might actually be worth it */
  }
  if (utype_has_flag(punittype, UTYF_CITYBUSTER)) {
    desire += desire / 2;
  }
  if (can_attack_from_non_native(punittype)) {
    desire += desire / 4;
  }
  if (punittype->adv.igwall) {
    desire += desire / 4;
  }

  return desire;
}

/**********************************************************************//**
  What would be the best defender for that city? Records the best defender
  type in choice. Also sets the technology want for the units we can't
  build yet.
**************************************************************************/
bool dai_process_defender_want(struct ai_type *ait, const struct civ_map *nmap,
                               struct player *pplayer,
                               struct city *pcity, unsigned int danger,
                               struct adv_choice *choice, adv_want extra_want)
{
  const struct research *presearch = research_get(pplayer);
  /* FIXME: We check if the city has *some* defensive structure,
   * but not whether the city has a defensive structure against
   * any specific attacker. The actual danger may not be mitigated
   * by the defense selected... */
  bool walls = city_got_defense_effect(pcity, NULL);
  /* Technologies we would like to have. */
  adv_want tech_desire[U_LAST];
  /* Our favourite unit. */
  adv_want best = -1;
  struct unit_type *best_unit_type = NULL;
  int best_unit_cost = 1;
  struct ai_city *city_data = def_ai_city_data(pcity, ait);
  struct ai_plr *plr_data = def_ai_player_data(pplayer, ait);
  adv_want total_want = danger + extra_want;

  memset(tech_desire, 0, sizeof(tech_desire));

  simple_ai_unit_type_iterate(punittype) {
    adv_want desire; /* How much we want the unit? */

    /* Only consider proper defenders - otherwise waste CPU and
     * bump tech want needlessly. */
    if (!utype_has_role(punittype, L_DEFEND_GOOD)
        && !utype_has_role(punittype, L_DEFEND_OK)) {
      continue;
    }

    desire = dai_unit_defense_desirability(ait, punittype);

    if (!utype_has_role(punittype, L_DEFEND_GOOD)) {
      desire /= 2; /* Not good, just ok */
    }

    if (utype_has_flag(punittype, UTYF_FIELDUNIT)) {
      /* Causes unhappiness even when in defense, so not a good
       * idea for a defender, unless it is _really_ good.
       * Downright counter productive, if we want unit just for
       * maintaining peace. */
      if (danger == 0) {
        desire = -50;
      } else {
        desire /= 2;
      }
    }

    if (desire > 0) {
      desire /= POWER_DIVIDER / 2; /* Good enough, no rounding errors. */
      desire *= desire;

      if (can_city_build_unit_now(nmap, pcity, punittype)) {
        /* We can build the unit now... */

        int build_cost = utype_build_shield_cost(pcity, NULL, punittype);
        int limit_cost = pcity->shield_stock + 40;

        if (walls && !utype_has_flag(punittype, UTYF_BADCITYDEFENDER)) {
          desire *= city_data->wallvalue;
          /* TODO: More use of POWER_FACTOR ! */
          desire /= POWER_FACTOR;
        }

        if ((best_unit_cost > limit_cost
             && build_cost < best_unit_cost)
            || ((desire > best
                 || (ADV_WANTS_EQ(desire, best)
                     && build_cost <= best_unit_cost))
                && (best_unit_type == NULL
                    /* In case all units are more expensive than limit_cost */
                    || limit_cost <= pcity->shield_stock + 40))) {
          best = desire;
          best_unit_type = punittype;
          best_unit_cost = build_cost;
        }
      } else if (can_city_build_unit_later(nmap, pcity, punittype)) {
        /* We first need to develop the tech required by the unit... */

        /* Cost (shield equivalent) of gaining these techs. */
        /* FIXME? Katvrr advises that this should be weighted more heavily in
         * big danger. */
        int tech_cost = 0;

        unit_tech_reqs_iterate(punittype, padv) {
          tech_cost = research_goal_bulbs_required(presearch,
                                                   advance_number(padv));
        } unit_tech_reqs_iterate_end;

        tech_cost = tech_cost / 4 / city_list_size(pplayer->cities);

        /* Contrary to the above, we don't care if walls are actually built
         * - we're looking into the future now. */
        if (!utype_has_flag(punittype, UTYF_BADCITYDEFENDER)) {
          desire *= city_data->wallvalue;
          desire /= POWER_FACTOR;
        }

        /* Yes, there's some similarity with kill_desire(). */
        /* TODO: Explain what shield cost has to do with tech want. */
        tech_desire[utype_index(punittype)] =
          (desire * total_want
           / (utype_build_shield_cost(pcity, NULL, punittype) + tech_cost));
      }
    }
  } simple_ai_unit_type_iterate_end;

  if (best == -1) {
    CITY_LOG(LOG_DEBUG, pcity, "Ooops - we cannot build any defender!");
  }

  if (best_unit_type != NULL) {
    if (!walls && !utype_has_flag(best_unit_type, UTYF_BADCITYDEFENDER)) {
      best *= city_data->wallvalue;
      best /= POWER_FACTOR;
    }
  } else {
    best_unit_cost = 100; /* Building impossible is considered costly.
                           * This should increase want for tech providing
                           * first defender type. */
  }

  if (best <= 0) {
    best = 1; /* Avoid division by zero below. */
  }

  /* Update tech_want for appropriate techs for units we want to build. */
  simple_ai_unit_type_iterate(punittype) {
    if (tech_desire[utype_index(punittype)] > 0) {
      /* TODO: Document or fix the algorithm below. I have no idea why
       * it is written this way, and the results seem strange to me. - Per */
      adv_want desire = tech_desire[utype_index(punittype)] * best_unit_cost / best;

      unit_tech_reqs_iterate(punittype, padv) {
        plr_data->tech_want[advance_index(padv)]
          += desire;
        TECH_LOG(ait, LOG_DEBUG, pplayer, padv,
                 "+ " ADV_WANT_PRINTF " for %s to defend %s",
                 desire,
                 utype_rule_name(punittype),
                 city_name_get(pcity));
      } unit_tech_reqs_iterate_end;
    }
  } simple_ai_unit_type_iterate_end;

  if (!best_unit_type) {
    return FALSE;
  }

  choice->value.utype = best_unit_type;
  choice->want = danger; /* FIXME: Not 'total_want' because of the way callers
                          * are constructed. They may overwrite this value,
                          * and then the value will NOT contain 'extra_want'.
                          * Later we may add 'extra_want', and don't want it
                          * already included in case this was NOT overwritten. */
  choice->type = CT_DEFENDER;

  return TRUE;
}

/**********************************************************************//**
  This function decides, what unit would be best for erasing enemy. It is
  called, when we just want to kill something, we've found it but we don't
  have the unit for killing that built yet - here we'll choose the type of
  that unit.

  We will also set increase the technology want to get units which could
  perform the job better.

  I decided this funct wasn't confusing enough, so I made
  kill_something_with() send it some more variables for it to meddle with.
  -- Syela

  'ptile' is location of the target.
  best_choice is pre-filled with our current choice, we only
  consider units who can move in all the same terrains for best_choice.
**************************************************************************/
static void process_attacker_want(struct ai_type *ait,
                                  struct city *pcity,
                                  adv_want value,
                                  const struct unit_type *victim_unit_type,
                                  struct player *victim_player,
                                  int veteran, struct tile *ptile,
                                  struct adv_choice *best_choice,
                                  struct pf_map *ferry_map,
                                  struct unit *boat,
                                  const struct unit_type *boattype)
{
  struct player *pplayer = city_owner(pcity);
  const struct research *presearch = research_get(pplayer);
  /* The enemy city.  acity == NULL means stray enemy unit */
  struct city *acity = tile_city(ptile);
  struct pf_parameter parameter;
  struct pf_map *pfm;
  struct pf_position pos;
  const struct unit_type *orig_utype = best_choice->value.utype;
  int victim_count = 1;
  int needferry = 0;
  bool unhap;
  struct ai_plr *plr_data = def_ai_player_data(pplayer, ait);
  const struct civ_map *nmap = &(wld.map);

  /* Has to be initialized to make gcc happy */
  struct ai_city *acity_data = NULL;

  unhap = dai_assess_military_unhappiness(nmap, pcity);

  if (acity != NULL) {
    acity_data = def_ai_city_data(acity, ait);
  }

  if (utype_class(orig_utype)->adv.sea_move == MOVE_NONE
      && !boat && boattype != NULL) {
    /* Cost of ferry */
    needferry = utype_build_shield_cost(pcity, NULL, boattype);
  }

  if (!is_stack_vulnerable(ptile)) {
    /* If it is a city, a fortress or an air base,
     * we may have to whack it many times */
    victim_count += unit_list_size(ptile->units);
  }

  simple_ai_unit_type_iterate(punittype) {
    if (dai_can_unit_type_follow_unit_type(punittype, orig_utype, ait)
        && is_native_near_tile(&(wld.map), utype_class(punittype), ptile)
        && (U_NOT_OBSOLETED == punittype->obsoleted_by
            || !can_city_build_unit_direct(nmap, pcity, punittype->obsoleted_by))
        && punittype->attack_strength > 0 /* Or we'll get SIGFPE */) {
      /* Values to be computed */
      adv_want desire;
      adv_want want;
      int move_time;
      int vuln;
      int veteran_level
        = get_target_bonus_effects(NULL,
                                   &(const struct req_context) {
                                     .player = pplayer,
                                     .city = pcity,
                                     .tile = city_tile(pcity),
                                     .unittype = punittype,
                                   },
                                   NULL,
                                   EFT_VETERAN_BUILD);
      /* Cost (shield equivalent) of gaining these techs. */
      /* FIXME? Katvrr advises that this should be weighted more heavily in big
       * danger. */
      int tech_cost = 0;
      int bcost_balanced = build_cost_balanced(punittype);
      /* See description of kill_desire() for info about this variables. */
      int bcost = utype_build_shield_cost(pcity, NULL, punittype);
      int attack = adv_unittype_att_rating(punittype, veteran_level,
                                           SINGLE_MOVE,
                                           punittype->hp);
      int tech_dist = 0;

      unit_tech_reqs_iterate(punittype, padv) {
        Tech_type_id tech_req = advance_number(padv);

        tech_cost += research_goal_bulbs_required(presearch, tech_req);
        if (tech_dist == 0) {
          tech_dist = research_goal_unknown_techs(presearch, tech_req);
        }
      } unit_tech_reqs_iterate_end;

      tech_cost = tech_cost / 4 / city_list_size(pplayer->cities);

      /* Take into account reinforcements strength */
      if (acity) {
        attack += acity_data->attack;
      }

      if (attack == 0) {
       /* Yes, it can happen that a military unit has attack = 1,
        * for example militia with HP = 1 (in civ1 ruleset). */
        continue;
      }

      attack *= attack;

      pft_fill_utype_parameter(&parameter, nmap, punittype, city_tile(pcity),
                               pplayer);
      parameter.omniscience = !has_handicap(pplayer, H_MAP);
      pfm = pf_map_new(&parameter);

      /* Set the move_time appropriately. */
      move_time = -1;
      if (NULL != ferry_map) {
        struct tile *dest_tile;

        if (find_beachhead(pplayer, ferry_map, ptile, punittype,
                           boattype, &dest_tile, NULL)
            && pf_map_position(ferry_map, dest_tile, &pos)) {
          move_time = pos.turn;
          dest_tile = pf_map_parameter(ferry_map)->start_tile;
          pf_map_tiles_iterate(pfm, atile, FALSE) {
            if (atile == dest_tile) {
              pf_map_iter_position(pfm, &pos);
              move_time += pos.turn;
              break;
            } else if (atile == ptile) {
              /* Reaching directly seems better. */
              pf_map_iter_position(pfm, &pos);
              move_time = pos.turn;
              break;
            }
          } pf_map_tiles_iterate_end;
        }
      }

      if (-1 == move_time) {
        if (pf_map_position(pfm, ptile, &pos)) {
          move_time = pos.turn;
        } else {
          pf_map_destroy(pfm);
          continue;
        }
      }
      pf_map_destroy(pfm);

      /* Estimate strength of the enemy. */

      if (victim_unit_type) {
        vuln = unittype_def_rating_squared(punittype, victim_unit_type,
                                           victim_player,
                                           ptile, FALSE, veteran);
      } else {
        vuln = 0;
      }

      /* Not bothering to s/!vuln/!pdef/ here for the time being. -- Syela
       * (this is noted elsewhere as terrible bug making warships yoyoing)
       * as the warships will go to enemy cities hoping that the enemy builds
       * something for them to kill. */
      if (vuln == 0
          && (utype_class(punittype)->adv.land_move == MOVE_NONE
              || 0 < utype_fuel(punittype))) {
        desire = 0;

      } else {
        if (acity
            && utype_can_take_over(punittype)
            && acity_data->invasion.attack > 0
            && acity_data->invasion.occupy == 0) {
          int owner_size = city_list_size(city_owner(acity)->cities);
          float finishing_factor = 1;

          if (owner_size <= FINISH_HIM_CITY_COUNT) {
            finishing_factor = (2 - (float)owner_size / FINISH_HIM_CITY_COUNT);
          }
          desire = CITY_CONQUEST_WORTH(acity, acity_data) * 10 * finishing_factor;
        } else {
          desire = 0;
        }

        if (!acity) {
          desire = kill_desire(value, attack, bcost, vuln, victim_count);
        } else {
          adv_want kd;
          int city_attack = acity_data->attack * acity_data->attack;

          /* See daiunit.c:find_something_to_kill() for comments. */
          kd = kill_desire(value, attack,
                           (bcost + acity_data->bcost), vuln,
                           victim_count);

          if (value * city_attack > acity_data->bcost * vuln) {
            kd -= kill_desire(value, city_attack,
                              acity_data->bcost, vuln,
                              victim_count);
          }

          desire = MAX(desire, kd);
        }
      }

      desire -= tech_cost * SHIELD_WEIGHTING;
      /* We can be possibly making some people of our homecity unhappy - then
       * we don't want to travel too far away to our victims. */
      /* TODO: Unify the 'unhap' dependency to some common function. */
      desire -= move_time * (unhap ? SHIELD_WEIGHTING + 2 * TRADE_WEIGHTING
                             : SHIELD_WEIGHTING);

      want = military_amortize(pplayer, pcity, desire, MAX(1, move_time),
                               bcost_balanced + needferry);

      if (want > 0) {
        if (tech_dist > 0) {
          /* This is a future unit, tell the scientist how much we need it */
          unit_tech_reqs_iterate(punittype, padv) {
            plr_data->tech_want[advance_index(padv)]
              += want;
            TECH_LOG(ait, LOG_DEBUG, pplayer, padv,
                     "+ " ADV_WANT_PRINTF " for %s vs %s(%d,%d)",
                     want,
                     utype_rule_name(punittype),
                     (acity ? city_name_get(acity) : utype_rule_name(victim_unit_type)),
                     TILE_XY(ptile));
          } unit_tech_reqs_iterate_end;
        } else if (want > best_choice->want) {
          const struct impr_type *impr_req;

          if (can_city_build_unit_now(nmap, pcity, punittype)) {
            /* This is a real unit and we really want it */

            CITY_LOG(LOG_DEBUG, pcity, "overriding %s(" ADV_WANT_PRINTF
                     ") with %s(" ADV_WANT_PRINTF ")"
                     " [attack=%d,value=" ADV_WANT_PRINTF
                     ",move_time=%d,vuln=%d,bcost=%d]",
                     utype_rule_name(best_choice->value.utype),
                     best_choice->want,
                     utype_rule_name(punittype),
                     want,
                     attack, value, move_time, vuln, bcost);

            best_choice->value.utype = punittype;
            best_choice->want = want;
            best_choice->type = CT_ATTACKER;
          } else if (!((impr_req = utype_needs_improvement(punittype,
                                                           pcity)))) {
            CITY_LOG(LOG_DEBUG, pcity, "cannot build unit %s",
                     utype_rule_name(punittype));
          } else if (can_city_build_improvement_now(pcity, impr_req)) {
            /* Building this unit requires a specific type of improvement.
             * So we build this improvement instead.  This may not be the
             * best behavior. */
            CITY_LOG(LOG_DEBUG, pcity, "building %s to build unit %s",
                     improvement_rule_name(impr_req),
                     utype_rule_name(punittype));
            best_choice->value.building = impr_req;
            best_choice->want = want;
            best_choice->type = CT_BUILDING;
          } else {
            /* This should never happen? */
            CITY_LOG(LOG_DEBUG, pcity, "cannot build %s or unit %s",
                     improvement_rule_name(impr_req),
                     utype_rule_name(punittype));
          }
        }
      }
    }
  } simple_ai_unit_type_iterate_end;
}

/**********************************************************************//**
  This function
  1. receives (in myunit) a first estimate of what we would like to build.
  2. finds a potential victim for it.
  3. calculates the relevant stats of the victim.
  4. finds the best attacker for this type of victim
     (in process_attacker_want() )
  5. if we still want to attack, records the best attacker in choice.
  If the target is overseas, the function might suggest building a ferry
  to carry a land attack unit, instead of the land attack unit itself.
**************************************************************************/
static struct adv_choice *kill_something_with(struct ai_type *ait,
                                              const struct civ_map *nmap,
                                              struct player *pplayer,
                                              struct city *pcity, struct unit *myunit,
                                              struct adv_choice *choice)
{
  /* Our attack rating (with reinforcements) */
  int attack;
  /* Benefit from fighting the target */
  adv_want benefit;
  /* Defender of the target city/tile */
  struct unit *pdef;
  const struct unit_type *def_type;
  struct player *def_owner;
  int def_vet; /* Is the defender veteran? */
  /* Target coordinates */
  struct tile *ptile;
  /* Our transport */
  struct unit *ferryboat;
  /* Our target */
  struct city *acity;
  /* Type of the boat (real or a future one) */
  const struct unit_type *boattype;
  struct pf_map *ferry_map = NULL;
  int move_time;
  struct adv_choice *best_choice;
  struct ai_city *city_data = def_ai_city_data(pcity, ait);
  struct ai_city *acity_data;

  best_choice = adv_new_choice();
  best_choice->value.utype = unit_type_get(myunit);
  best_choice->type = CT_ATTACKER;
  adv_choice_set_use(best_choice, "attacker");

  fc_assert_ret_val(!is_special_unit(myunit)
                    && !utype_fuel(unit_type_get(myunit)), choice);

  if (city_data->danger != 0 && assess_defense(ait, pcity) == 0) {
    /* Defense comes first! */
    goto cleanup;
  }

  best_choice->want = find_something_to_kill(ait, pplayer, myunit, &ptile, NULL,
                                             &ferry_map, &ferryboat,
                                             &boattype, &move_time);
  if (NULL == ptile
      || ptile == unit_tile(myunit)
      || !can_unit_attack_tile(myunit, NULL, ptile)) {
    goto cleanup;
  }

  acity = tile_city(ptile);

  if (myunit->id != 0) {
    log_error("%s(): non-virtual unit!", __FUNCTION__);
    goto cleanup;
  }

  attack = adv_unit_att_rating(myunit);
  if (acity) {
    acity_data = def_ai_city_data(acity, ait);
    attack += acity_data->attack;
  }
  attack *= attack;

  if (NULL != acity) {
    /* Rating of enemy defender */
    int vulnerability;

    if (!POTENTIALLY_HOSTILE_PLAYER(ait, pplayer, city_owner(acity))) {
      /* Not a valid target */
      goto cleanup;
    }

    def_type = dai_choose_defender_versus(acity, myunit);
    def_owner = city_owner(acity);
    if (1 < move_time && def_type) {
      def_vet = city_production_unit_veteran_level(acity, def_type);
      vulnerability = unittype_def_rating_squared(unit_type_get(myunit), def_type,
                                                  city_owner(acity), ptile,
                                                  FALSE, def_vet);
      benefit = utype_build_shield_cost_base(def_type);
    } else {
      vulnerability = 0;
      benefit = 0;
      def_vet = 0;
    }

    pdef = get_defender(nmap, myunit, ptile, NULL);
    if (pdef) {
      int m = unittype_def_rating_squared(unit_type_get(myunit), unit_type_get(pdef),
                                          city_owner(acity), ptile, FALSE,
                                          pdef->veteran);
      if (vulnerability < m) {
        benefit = unit_build_shield_cost_base(pdef);
        def_vet = pdef->veteran;
        def_type = unit_type_get(pdef);
        def_owner = unit_owner(pdef);
      }
    }
    if (unit_can_take_over(myunit) || acity_data->invasion.occupy > 0) {
      /* Bonus for getting the city */
      int owner_size = city_list_size(city_owner(acity)->cities);
      float finishing_factor = 1;

      if (owner_size <= FINISH_HIM_CITY_COUNT) {
        finishing_factor = (2 - (float)owner_size / FINISH_HIM_CITY_COUNT);
      }
      benefit += CITY_CONQUEST_WORTH(acity, acity_data) * finishing_factor / 3;
    }

    /* end dealing with cities */
  } else {

    if (NULL != ferry_map) {
      pf_map_destroy(ferry_map);
      ferry_map = NULL;
    }

    pdef = get_defender(nmap, myunit, ptile, NULL);
    if (!pdef) {
      /* Nobody to attack! */
      goto cleanup;
    }

    benefit = unit_build_shield_cost_base(pdef);

    def_type = unit_type_get(pdef);
    def_vet = pdef->veteran;
    def_owner = unit_owner(pdef);
    /* end dealing with units */
  }

  if (NULL == ferry_map) {
    process_attacker_want(ait, pcity, benefit, def_type, def_owner,
                          def_vet, ptile,
                          best_choice, NULL, NULL, NULL);
  } else {
    /* Attract a boat to our city or retain the one that's already here */
    fc_assert_ret_val(unit_class_get(myunit)->adv.sea_move != MOVE_FULL, choice);
    best_choice->need_boat = TRUE;
    process_attacker_want(ait, pcity, benefit, def_type, def_owner,
                          def_vet, ptile,
                          best_choice, ferry_map, ferryboat, boattype);
  }

  if (best_choice->want > choice->want) {
    /* We want attacker more than what we have selected before */
    adv_free_choice(choice);
    choice = best_choice;
    CITY_LOG(LOG_DEBUG, pcity, "kill_something_with()"
             " %s has chosen attacker, %s, want=" ADV_WANT_PRINTF,
             city_name_get(pcity),
             utype_rule_name(best_choice->value.utype),
             best_choice->want);

    if (NULL != ferry_map && !ferryboat) { /* need a new ferry */
      /* We might need a new boat even if there are boats free,
       * if they are blockaded or in inland seas*/
      fc_assert_ret_val(unit_class_get(myunit)->adv.sea_move != MOVE_FULL, choice);
      if (dai_choose_role_unit(ait, pplayer, pcity, choice, CT_ATTACKER,
			       L_FERRYBOAT, choice->want, TRUE)
	  && dai_is_ferry_type(choice->value.utype, ait)) {
#ifdef FREECIV_DEBUG
        struct ai_plr *ai = dai_plr_data_get(ait, pplayer, NULL);

        log_debug("kill_something_with() %s has chosen attacker ferry, "
                  "%s, want=" ADV_WANT_PRINTF ", %d of %d free",
                  city_name_get(pcity),
                  utype_rule_name(choice->value.utype),
                  choice->want,
                  ai->stats.available_boats, ai->stats.boats);
#endif /* FREECIV_DEBUG */

        adv_choice_set_use(choice, "attacker ferry");
      } /* else can not build ferries yet */
    }
  }

cleanup:
  if (best_choice != choice) {
    /* It was not taken to use.
     * This hackery needed since 'goto cleanup:' might skip
     * sensible points to do adv_free_choice(). */
    adv_free_choice(best_choice);
  }
  if (NULL != ferry_map) {
    pf_map_destroy(ferry_map);
  }

  return choice;
}

/**********************************************************************//**
  This function should assign a value to choice and want and type,
  where want is a value between 0.0 and
  DAI_WANT_MILITARY_EMERGENCY (not inclusive)
  if want is 0 this advisor doesn't want anything
**************************************************************************/
static void dai_unit_consider_bodyguard(struct ai_type *ait,
                                        const struct civ_map *nmap,
                                        struct city *pcity,
                                        struct unit_type *punittype,
                                        struct adv_choice *choice)
{
  if (choice->want < DAI_WANT_MILITARY_EMERGENCY) {
    struct player *pplayer = city_owner(pcity);
    struct unit *aunit = NULL;
    struct city *acity = NULL;
    struct unit *virtualunit
      = unit_virtual_create(pplayer, pcity, punittype,
                            city_production_unit_veteran_level(pcity,
                                                               punittype));
    const adv_want want = look_for_charge(ait, nmap, pplayer, virtualunit,
                                          &aunit, &acity);

    if (want > choice->want) {
      choice->want = want;
      choice->value.utype = punittype;
      choice->type = CT_DEFENDER;
      adv_choice_set_use(choice, "bodyguard");
    }

    unit_virtual_destroy(virtualunit);
  }
}

/**********************************************************************//**
  Before building a military unit, AI builds a barracks/port/airport
  NB: It is assumed this function isn't called in an emergency
  situation, when we need a defender _now_.

  TODO: something more sophisticated, like estimating future demand
  for military units, considering Sun Tzu instead.
**************************************************************************/
static void adjust_ai_unit_choice(struct city *pcity,
                                  struct adv_choice *choice)
{
  Impr_type_id id;

  /* Sanity */
  if (!is_unit_choice_type(choice->type)
      || utype_has_flag(choice->value.utype, UTYF_CIVILIAN)
      || city_production_unit_veteran_level(pcity, choice->value.utype)) {
    return;
  }

  /*  N.B.: have to check that we haven't already built the building --mck */
  if ((id = dai_find_source_building(pcity, EFT_VETERAN_BUILD,
                                     choice->value.utype)) != B_LAST
       && !city_has_building(pcity, improvement_by_number(id))) {
    choice->value.building = improvement_by_number(id);
    choice->want = choice->want * (0.5 + (ai_trait_get_value(TRAIT_BUILDER,
                                                             city_owner(pcity))
                                          / TRAIT_DEFAULT_VALUE / 2));
    choice->type = CT_BUILDING;
    adv_choice_set_use(choice, "veterancy building");
  }
}

/**********************************************************************//**
  This function selects either a defender or an attacker to be built.
  It records its choice into adv_choice struct.
  If 'choice->want' is 0 this advisor doesn't want anything.
**************************************************************************/
struct adv_choice *military_advisor_choose_build(struct ai_type *ait,
                                                 const struct civ_map *nmap,
                                                 struct player *pplayer,
                                                 struct city *pcity,
                                                 player_unit_list_getter ul_cb)
{
  struct adv_data *ai = adv_data_get(pplayer, NULL);
  struct unit_type *punittype;
  unsigned int our_def, urgency;
  struct tile *ptile = pcity->tile;
  struct unit *virtualunit;
  struct ai_city *city_data = def_ai_city_data(pcity, ait);
  adv_want martial_value = 0;
  bool martial_need = FALSE;
  struct adv_choice *choice = adv_new_choice();
  bool allow_gold_upkeep;

  urgency = assess_danger(ait, nmap, pcity, ul_cb);
  /* Changing to quadratic to stop AI from building piles
   * of small units -- Syela */
  /* It has to be AFTER assess_danger() thanks to wallvalue. */
  our_def = assess_defense_quadratic(ait, pcity);

  dai_choose_diplomat_defensive(ait, pplayer, pcity, choice, our_def);

  if (pcity->feel[CITIZEN_UNHAPPY][FEELING_NATIONALITY]
      + pcity->feel[CITIZEN_ANGRY][FEELING_NATIONALITY] > 0) {
    martial_need = TRUE;
  }

  if (!martial_need) {
    normal_specialist_type_iterate(sp) {
      if (pcity->specialists[sp] > 0
          && get_specialist_output(pcity, sp, O_LUXURY) > 0) {
        martial_need = TRUE;
        break;
      }
    } normal_specialist_type_iterate_end;
  }

  if (martial_need
      && unit_list_size(pcity->tile->units) < get_city_bonus(pcity, EFT_MARTIAL_LAW_MAX)) {
    martial_value = dai_content_effect_value(pplayer, pcity,
                                             get_city_bonus(pcity, EFT_MARTIAL_LAW_BY_UNIT),
                                             1, FEELING_FINAL);
  }

  /* Otherwise no need to defend yet */
  if (city_data->danger != 0 || martial_value > 0) {
    struct impr_type *pimprove;
    int num_defenders = unit_list_size(ptile->units);
    int wall_id, danger;
    bool build_walls = TRUE;
    bool def_unit_selected = FALSE;
    int qdanger = city_data->danger * city_data->danger;

    if (qdanger <= 0) {
      /* We only need these defenders because of Martial Law value */
      danger = 0;
      build_walls = FALSE; /* Walls don't provide Martial Law */
    } else {
      /* First determine the danger. It is measured in percents of our
       * defensive strength, capped at 200 + urgency */
      if (qdanger >= our_def) {
        if (urgency == 0) {
          /* Don't waste money */
          danger = 100;
        } else if (our_def == 0) {
          danger = 200 + urgency;
        } else {
          danger = MIN(200, 100 * qdanger / our_def) + urgency;
        }
      } else {
        danger = 100 * qdanger / our_def;
      }

      if (pcity->surplus[O_SHIELD] <= 0 && our_def != 0) {
        /* Won't be able to support anything */
        danger = 0;
      }
    }

    CITY_LOG(LOG_DEBUG, pcity, "m_a_c_d urgency=%d danger=%d num_def=%d "
             "our_def=%d", urgency, danger, num_defenders, our_def);

    if (our_def == 0 && danger > 0) {
      /* Build defensive unit first! Walls will help nothing if there's
       * nobody behind them. */
      if (dai_process_defender_want(ait, nmap, pplayer, pcity, danger, choice,
                                    martial_value)) {
        choice->want = DAI_WANT_BELOW_MIL_EMERGENCY + danger;
        adv_choice_set_use(choice, "first defender");
        build_walls = FALSE;
        def_unit_selected = TRUE;

        CITY_LOG(LOG_DEBUG, pcity, "m_a_c_d wants first defender with " ADV_WANT_PRINTF,
                 choice->want);
      }
    }
    if (build_walls) {
      /* FIXME: 1. Does not consider what kind of future danger is likely, so
       * may build SAM batteries when enemy has only land units. */
      /* We will build walls if we can and want and (have "enough" defenders or
       * can just buy the walls straight away) */

      /* HACK: This needs changing if multiple improvements provide
       * this effect. */
      wall_id = dai_find_source_building(pcity, EFT_DEFEND_BONUS, NULL);
      pimprove = improvement_by_number(wall_id);

      if (wall_id != B_LAST
          && pcity->server.adv->building_want[wall_id] != 0 && our_def != 0
          && can_city_build_improvement_now(pcity, pimprove)
          && (danger < 101 || num_defenders > 1
              || (city_data->grave_danger == 0
                  && pplayer->economic.gold
                     > impr_buy_gold_cost(pcity, pimprove, pcity->shield_stock)))
          && ai_fuzzy(pplayer, TRUE)) {
        if (pcity->server.adv->building_want[wall_id] > 0) {
          /* NB: great wall is under domestic */
          choice->value.building = pimprove;
          /* building_want is hacked by assess_danger() */
          choice->want = pcity->server.adv->building_want[wall_id];
          choice->want = choice->want * (0.5 + (ai_trait_get_value(TRAIT_BUILDER, pplayer)
                                                / TRAIT_DEFAULT_VALUE / 2));
          if (urgency == 0 && choice->want > DAI_WANT_BELOW_MIL_EMERGENCY) {
            choice->want = DAI_WANT_BELOW_MIL_EMERGENCY;
          }
          choice->type = CT_BUILDING;
          adv_choice_set_use(choice, "defense building");
          CITY_LOG(LOG_DEBUG, pcity,
                   "m_a_c_d wants defense building with " ADV_WANT_PRINTF,
                   choice->want);
        } else {
          build_walls = FALSE;
        }
      } else {
        build_walls = FALSE;
      }
    }

    /* If our choice isn't defender unit already, consider one */
    if (!def_unit_selected) {
      if ((danger > 0 && num_defenders <= urgency) || martial_value > 0) {
        struct adv_choice uchoice;

        adv_init_choice(&uchoice);

        /* Consider building defensive units */
        if (dai_process_defender_want(ait, nmap, pplayer, pcity, danger,
                                      &uchoice, martial_value)) {
          /* Potential defender found */
          if (urgency == 0
              && uchoice.value.utype->defense_strength == 1) {
            /* FIXME: check other reqs (unit class?) */
            if (get_city_bonus(pcity, EFT_HP_REGEN) > 0) {
              /* Unlikely */
              uchoice.want = MIN(49, danger);
            } else {
              uchoice.want = MIN(25, danger);
            }
          } else {
            uchoice.want = danger;
          }

          uchoice.want += martial_value;
          if (danger > 0) {
            adv_choice_set_use(&uchoice, "defender");
          } else {
            adv_choice_set_use(&uchoice, "police");
          }

          if (!build_walls || uchoice.want > choice->want) {
            adv_choice_copy(choice, &uchoice);
          }
          adv_deinit_choice(&uchoice);

          CITY_LOG(LOG_DEBUG, pcity, "m_a_c_d wants %s with desire " ADV_WANT_PRINTF,
                   utype_rule_name(choice->value.utype),
                   choice->want);
        } else {
          CITY_LOG(LOG_DEBUG, pcity, "m_a_c_d cannot select defender");
        }
      } else {
        CITY_LOG(LOG_DEBUG, pcity, "m_a_c_d does not want defenders");
      }
    }
  } /* Ok, don't need to defend */

  if (pcity->surplus[O_SHIELD] <= 0
      || pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL] > pcity->feel[CITIZEN_UNHAPPY][FEELING_EFFECT]
      || pcity->id == ai->wonder_city) {
    /* Things we consider below are not life-saving so we don't want to
     * build them if our populace doesn't feel like it */
    return choice;
  }

  if (pplayer->economic.tax <= 50 || city_total_unit_gold_upkeep(pcity) <= 0) {
    /* Always allow one unit with real gold upkeep (after EFT_UNIT_UPKEEP_FREE_PER_CITY)
     * Allow more if economics is so strong that we have not increased taxes. */
    allow_gold_upkeep = TRUE;
  } else {
    allow_gold_upkeep = FALSE;
  }

  /* Consider making a land bodyguard */
  punittype = dai_choose_bodyguard(ait, nmap, pcity, TC_LAND, L_DEFEND_GOOD,
                                   allow_gold_upkeep);
  if (punittype) {
    dai_unit_consider_bodyguard(ait, nmap, pcity, punittype, choice);
  }

  /* If we are in severe danger, don't consider attackers. This is probably
     too general. In many cases we will want to buy attackers to counterattack.
     -- Per */
  if (choice->want - martial_value >= DAI_WANT_MILITARY_EMERGENCY
      && city_data->grave_danger > 0) {
    CITY_LOG(LOGLEVEL_BUILD, pcity,
             "severe danger (want " ADV_WANT_PRINTF "), force defender",
             choice->want);
    return choice;
  }

  /* Consider making an offensive diplomat */
  dai_choose_diplomat_offensive(ait, nmap, pplayer, pcity, choice);

  /* Consider making a sea bodyguard */
  punittype = dai_choose_bodyguard(ait, nmap, pcity, TC_OCEAN, L_DEFEND_GOOD,
                                   allow_gold_upkeep);
  if (punittype) {
    dai_unit_consider_bodyguard(ait, nmap, pcity, punittype, choice);
  }

  /* Consider making an airplane */
  (void) dai_choose_attacker_air(ait, nmap, pplayer, pcity, choice,
                                 allow_gold_upkeep);

  /* Consider making a paratrooper */
  dai_choose_paratrooper(ait, nmap, pplayer, pcity, choice, allow_gold_upkeep);

  /* Check if we want a sailing attacker. Have to put sailing first
     before we mung the seamap */
  punittype = dai_choose_attacker(ait, nmap, pcity, TC_OCEAN, allow_gold_upkeep);
  if (punittype) {
    virtualunit = unit_virtual_create(
      pplayer, pcity, punittype,
      city_production_unit_veteran_level(pcity, punittype));
    choice = kill_something_with(ait, nmap, pplayer, pcity, virtualunit, choice);
    unit_virtual_destroy(virtualunit);
  }

  /* Consider a land attacker or a ferried land attacker
   * (in which case, we might want a ferry before an attacker)
   */
  punittype = dai_choose_attacker(ait, nmap, pcity, TC_LAND, allow_gold_upkeep);
  if (punittype) {
    virtualunit = unit_virtual_create(pplayer, pcity, punittype, 1);
    choice = kill_something_with(ait, nmap, pplayer, pcity, virtualunit, choice);
    unit_virtual_destroy(virtualunit);
  }

  /* Consider a hunter */
  dai_hunter_choice(ait, pplayer, pcity, choice, allow_gold_upkeep);

  /* Consider veteran level enhancing buildings before non-urgent units */
  adjust_ai_unit_choice(pcity, choice);

  if (choice->want <= 0) {
    CITY_LOG(LOGLEVEL_BUILD, pcity, "military advisor has no advice");
  } else {
    CITY_LOG(LOGLEVEL_BUILD, pcity,
             "military advisor choice: %s (want " ADV_WANT_PRINTF ")",
             adv_choice_rule_name(choice),
             choice->want);
  }

  return choice;
}
