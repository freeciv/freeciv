/**********************************************************************
 Freeciv - Copyright (C) 2003 - The Freeciv Team
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

#include "city.h"
#include "combat.h"
#include "game.h"
#include "map.h"
#include "log.h"
#include "pf_tools.h"
#include "player.h"
#include "unit.h"

#include "citytools.h"
#include "settlers.h"
#include "unittools.h"

#include "aidata.h"
#include "ailog.h"
#include "aitools.h"
#include "aiunit.h"

#include "aihunt.h"

/**************************************************************************
  We don't need a hunter in this city if we already have one. Return 
  existing hunter if any.
**************************************************************************/
static struct unit *ai_hunter_find(struct player *pplayer, 
                                   struct city *pcity)
{
  unit_list_iterate(pcity->units_supported, punit) {
    if (ai_hunter_qualify(pplayer, punit)) {
      return punit;
    }
  } unit_list_iterate_end;
  unit_list_iterate(pcity->tile->units, punit) {
    if (ai_hunter_qualify(pplayer, punit)) {
      return punit;
    }
  } unit_list_iterate_end;

  return NULL;
}

/**************************************************************************
  Guess best hunter unit type.
**************************************************************************/
static Unit_Type_id ai_hunter_guess_best(struct city *pcity,
                                         enum unit_move_type umt)
{
  Unit_Type_id bestid = -1;
  int best = 0;

  unit_type_iterate(i) {
    struct unit_type *ut = get_unit_type(i);
    int desire;

    if (ut->move_type != umt || !can_build_unit(pcity, i)
        || ut->attack_strength < ut->transport_capacity) {
      continue;
    }

    desire = (ut->hp
              * ut->attack_strength
              * ut->firepower
              * ut->move_rate
              + ut->defense_strength) / MAX(UNITTYPE_COSTS(ut), 1);

    if (unit_type_flag(i, F_CARRIER)
        || unit_type_flag(i, F_MISSILE_CARRIER)) {
      desire += desire / 6;
    }
    if (unit_type_flag(i, F_IGTER)) {
      desire += desire / 2;
    }
    if (unit_type_flag(i, F_IGTIRED)) {
      desire += desire / 8;
    }
    if (unit_type_flag(i, F_PARTIAL_INVIS)) {
      desire += desire / 4;
    }
    if (unit_type_flag(i, F_NO_LAND_ATTACK)) {
      desire -= desire / 4; /* less flexibility */
    }
    /* Causes continual unhappiness */
    if (unit_type_flag(i, F_FIELDUNIT)) {
      desire /= 2;
    }

    desire = amortize(desire, ut->build_cost / MAX(pcity->shield_surplus, 1));

    if (desire > best) {
        best = desire;
        bestid = i;
    }
  } unit_type_iterate_end;

  return bestid;
}

/**************************************************************************
  Check if we want to build a missile for our hunter.
**************************************************************************/
static void ai_hunter_missile_want(struct player *pplayer,
                                   struct city *pcity,
                                   struct ai_choice *choice)
{
  int best = -1, best_unit_type = -1;
  bool have_hunter = FALSE;

  unit_list_iterate(pcity->tile->units, punit) {
    if (ai_hunter_qualify(pplayer, punit)
        && (unit_flag(punit, F_MISSILE_CARRIER)
            || unit_flag(punit, F_CARRIER))) {
      /* There is a potential hunter in our city which we can equip 
       * with a missile. Do it. */
      have_hunter = TRUE;
      break;
    }
  } unit_list_iterate_end;

  if (!have_hunter) {
    return;
  }

  unit_type_iterate(i) {
    struct unit_type *ut = get_unit_type(i);
    int desire;

    if (!BV_ISSET(ut->flags, F_MISSILE) || !can_build_unit(pcity, i)) {
      continue;
    }

    /* FIXME: We need to store some data that can tell us if
     * enemy transports are protected by anti-missile technology. 
     * In this case, want nuclear much more! */
    desire = (ut->hp
              * MIN(ut->attack_strength, 30) /* nuke fix */
              * ut->firepower
              * ut->move_rate) / UNITTYPE_COSTS(ut) + 1;

    /* Causes continual unhappiness */
    if (unit_type_flag(i, F_FIELDUNIT)) {
      desire /= 2;
    }

    desire = amortize(desire, ut->build_cost / MAX(pcity->shield_surplus, 1));

    if (desire > best) {
        best = desire;
        best_unit_type = i;
    }
  } unit_type_iterate_end;

  if (best > choice->want) {
    CITY_LOG(LOGLEVEL_HUNT, pcity, "pri missile w/ want %d", best);
    choice->choice = best_unit_type;
    choice->want = best;
    choice->type = CT_ATTACKER;
  } else if (best != -1) {
    CITY_LOG(LOGLEVEL_HUNT, pcity, "not pri missile w/ want %d"
             "(old want %d)", best, choice->want);
  }
}

/**************************************************************************
  Support function for ai_hunter_choice()
**************************************************************************/
static void eval_hunter_want(struct player *pplayer, struct city *pcity,
                             struct ai_choice *choice, int best_type,
                             int veteran)
{
  struct unit *virtualunit;
  int want = 0;

  virtualunit = create_unit_virtual(pplayer, pcity, best_type, veteran);
  want = ai_hunter_findjob(pplayer, virtualunit);
  destroy_unit_virtual(virtualunit);
  if (want > choice->want) {
    CITY_LOG(LOGLEVEL_HUNT, pcity, "pri hunter w/ want %d", want);
    choice->choice = best_type;
    choice->want = want;
    choice->type = CT_ATTACKER;
  }
}

/**************************************************************************
  Check if we want to build a hunter.
**************************************************************************/
void ai_hunter_choice(struct player *pplayer, struct city *pcity,
                      struct ai_choice *choice)
{
  int best_land_hunter = ai_hunter_guess_best(pcity, LAND_MOVING);
  int best_sea_hunter = ai_hunter_guess_best(pcity, SEA_MOVING);
  struct unit *hunter = ai_hunter_find(pplayer, pcity);

  if ((best_land_hunter == -1 && best_sea_hunter == -1)
      || is_barbarian(pplayer) || !pplayer->is_alive
      || ai_handicap(pplayer, H_TARGETS)) {
    return; /* None available */
  }
  if (hunter) {
    /* Maybe want missiles to go with a hunter instead? */
    ai_hunter_missile_want(pplayer, pcity, choice);
    return;
  }

  if (best_sea_hunter >= 0) {
    eval_hunter_want(pplayer, pcity, choice, best_sea_hunter, 
                     do_make_unit_veteran(pcity, best_sea_hunter));
  }
  if (best_land_hunter >= 0) {
    eval_hunter_want(pplayer, pcity, choice, best_land_hunter, 
                     do_make_unit_veteran(pcity, best_land_hunter));
  }
}

/**************************************************************************
  Does this unit qualify as a hunter? FIXME: Should also check for
  combat damage? Or should repair code preempt this code? Just saying
  FALSE for damaged units does NOT work.
**************************************************************************/
bool ai_hunter_qualify(struct player *pplayer, struct unit *punit)
{
  struct unit_type *punittype = get_unit_type(punit->type);

  if (is_barbarian(pplayer)
      || !(is_sailing_unit(punit) || is_ground_unit(punit))
      || punittype->move_rate < 2 * SINGLE_MOVE
      || ATTACK_POWER(punit) <= 1
      || punit->owner != pplayer->player_no) {
    return FALSE;
  }
  if (unit_flag(punit, F_PARTIAL_INVIS)) {
    return TRUE;
  }
  /* TODO: insert better algo here */
  return IS_ATTACKER(punit);
}

/**************************************************************************
  Return want for making this (possibly virtual) unit into a hunter. Sets
  punit->ai.target to target's id.
**************************************************************************/
int ai_hunter_findjob(struct player *pplayer, struct unit *punit)
{
  int best_id = -1, best_val = -1;

  assert(!is_barbarian(pplayer));
  assert(pplayer->is_alive);

  players_iterate(aplayer) {
    if (!aplayer->is_alive || !is_player_dangerous(pplayer, aplayer)) {
      continue;
    }
    /* Note that we need not (yet) be at war with aplayer */
    unit_list_iterate(aplayer->units, target) {
      struct tile *ptile = target->tile;
      int dist1, dist2, stackthreat = 0, stackcost = 0;
      struct unit *defender;

      if (ptile->city
          || TEST_BIT(target->ai.hunted, pplayer->player_no)
          || (!is_ocean(ptile->terrain) && is_sailing_unit(punit))
          || (!is_sailing_unit(target) && is_sailing_unit(punit))
          || (is_sailing_unit(target) && !is_sailing_unit(punit))
          || !goto_is_sane(punit, target->tile, TRUE)) {
        /* Can't hunt this one. */
        continue;
      }
      if (target->ai.cur_pos && target->ai.prev_pos) {
        dist1 = real_map_distance(punit->tile, *target->ai.cur_pos);
        dist2 = real_map_distance(punit->tile, *target->ai.prev_pos);
      } else {
        dist1 = dist2 = 0;
      }
      UNIT_LOG(LOGLEVEL_HUNT, punit, "considering chasing %s(%d, %d) id %d "
               "dist1 %d dist2 %d",
	       unit_type(target)->name, TILE_XY(target->tile),
               target->id, dist1, dist2);
      /* We can't attack units stationary in cities. */
      if (map_get_city(target->tile) 
          && (dist2 == 0 || dist1 == dist2)) {
        continue;
      }
      /* We can't chase if we aren't faster or on intercept vector */
      if (unit_type(punit)->move_rate < unit_type(target)->move_rate
          && dist1 >= dist2) {
        UNIT_LOG(LOGLEVEL_HUNT, punit, "giving up racing %s (%d, %d)->(%d, %d)",
                 unit_type(target)->name,
		 target->ai.prev_pos ? (*target->ai.prev_pos)->x : -1,
                 target->ai.prev_pos ? (*target->ai.prev_pos)->y : -1,
                 TILE_XY(target->tile));
        continue;
      }
      unit_list_iterate(ptile->units, sucker) {
        stackthreat += ATTACK_POWER(sucker);
        if (unit_flag(sucker, F_DIPLOMAT)) {
          stackthreat += 500;
        }
        stackcost += unit_type(sucker)->build_cost;
      } unit_list_iterate_end;
      defender = get_defender(punit, target->tile);
      if (stackcost < unit_type(punit)->build_cost
          && unit_win_chance(punit, defender) < 0.6) {
        UNIT_LOG(LOGLEVEL_HUNT, punit, "chickening out from attacking %s"
                 "(%d, %d)", unit_type(defender)->name,
                 TILE_XY(defender->tile));
        continue;
      }
      stackthreat *= 9; /* WAG */
      stackthreat += stackcost;
      stackthreat /= real_map_distance(punit->tile, target->tile) + 1;
      UNIT_LOG(LOGLEVEL_HUNT, punit, "considering hunting %s's %s(%d, %d) id "
               "id %d with want %d, dist1 %d, dist2 %d", 
               unit_owner(defender)->name, unit_type(defender)->name, 
               TILE_XY(defender->tile), defender->id, stackthreat, dist1,
               dist2);
      /* TO DO: probably ought to WAG down targets of players we are not (yet)
       * at war with */
      /* Ok, now we FINALLY have a candidate */
      if (stackthreat > best_val) {
        best_val = stackthreat;
        best_id = target->id;
      }
    } unit_list_iterate_end;
  } players_iterate_end;

  punit->ai.target = best_id;
  if (best_val < MORT) {
    /* Safety against silly missions. Unset our role. */
    best_val = -1;
  } else {
    UNIT_LOG(LOGLEVEL_HUNT, punit, "suggest chasing %d with want %d",
             best_id, best_val);
  }
  return best_val;
}

/**************************************************************************
  Try to shoot our target with a missile. Also shoot down anything that
  might attempt to intercept _us_. We assign missiles to a hunter in
  ai_unit_new_role().
**************************************************************************/
static void ai_hunter_try_launch(struct player *pplayer,
                                 struct unit *punit,
                                 struct unit *target)
{
  int target_sanity = target->id;
  struct pf_parameter parameter;
  struct pf_map *map;

  unit_list_iterate(punit->tile->units, missile) {
    struct unit *sucker = NULL;

    if (missile->owner == pplayer->player_no
        && unit_flag(missile, F_MISSILE)) {
      UNIT_LOG(LOGLEVEL_HUNT, missile, "checking for hunt targets");
      pft_fill_unit_parameter(&parameter, punit);
      map = pf_create_map(&parameter);

      pf_iterator(map, pos) {
        if (pos.total_MC > missile->moves_left / SINGLE_MOVE) {
          break;
        }
        if (map_get_city(pos.tile)
            || !can_unit_attack_tile(punit, pos.tile)) {
          continue;
        }
        unit_list_iterate(pos.tile->units, victim) {
          struct unit_type *ut = unit_type(victim);
          enum diplstate_type ds = pplayer_get_diplstate(pplayer, 
                                                         unit_owner(victim))->type;

          if (ds != DS_WAR) {
            continue;
          }
          if (victim == target) {
            sucker = victim;
            UNIT_LOG(LOGLEVEL_HUNT, missile, "found primary target %d(%d, %d)"
                     " dist %d", victim->id, TILE_XY(victim->tile), 
                     pos.total_MC);
            break; /* Our target! Get him!!! */
          }
          if (ut->move_rate + victim->moves_left > pos.total_MC
              && ATTACK_POWER(victim) > DEFENCE_POWER(punit)
              && (ut->move_type == SEA_MOVING
                  || ut->move_type == AIR_MOVING)) {
            /* Threat to our carrier. Kill it. */
            sucker = victim;
            UNIT_LOG(LOGLEVEL_HUNT, missile, "found aux target %d(%d, %d)",
                     victim->id, TILE_XY(victim->tile));
            break;
          }
        } unit_list_iterate_end;
        if (sucker) {
          break; /* found something - kill it! */
        }
      } pf_iterator_end;
      pf_destroy_map(map);
      if (sucker) {
        if (find_unit_by_id(missile->transported_by)) {
          unload_unit_from_transporter(missile);
        }
        ai_unit_goto(missile, sucker->tile);
        sucker = find_unit_by_id(target_sanity); /* Sanity */
        if (sucker && is_tiles_adjacent(sucker->tile, missile->tile)) {
          ai_unit_attack(missile, sucker->tile);
        }
        target = find_unit_by_id(target_sanity); /* Sanity */
        break; /* try next missile, if any */
      }
    } /* if */
  } unit_list_iterate_end;
}

/**************************************************************************
  Manage a hunter unit. We assume it has AIUNIT_HUNTER role and a valid
  target in punit->ai.hunt.

  Returns FALSE if we could not use unit. If we return TRUE, unit might
  be dead.
**************************************************************************/
bool ai_hunter_manage(struct player *pplayer, struct unit *punit)
{
  struct unit *target = find_unit_by_id(punit->ai.target);
  int sanity_own = punit->id;
  int sanity_target;

  CHECK_UNIT(punit);
  assert(punit->ai.ai_role == AIUNIT_HUNTER);

  /* Check that target is valid. */
  if (!target
      || !goto_is_sane(punit, target->tile, TRUE)
      || map_get_city(target->tile)
      || !is_player_dangerous(pplayer, unit_owner(target))) {
    UNIT_LOG(LOGLEVEL_HUNT, punit, "target vanished");
    ai_unit_new_role(punit, AIUNIT_NONE, NULL);
    return FALSE;
  }
  UNIT_LOG(LOGLEVEL_HUNT, punit, "hunting %d(%d, %d)",
	   target->id, TILE_XY(target->tile));
  sanity_target = target->id;

  /* Check if we can nuke it */
  ai_hunter_try_launch(pplayer, punit, target);
  target = find_unit_by_id(sanity_target);
  if (!target){
    UNIT_LOG(LOGLEVEL_HUNT, punit, "mission accomplished");
    ai_unit_new_role(punit, AIUNIT_NONE, NULL);
    return TRUE;
  }

  /* Go towards it. */
  if (!ai_unit_goto(punit, target->tile)) {
    return TRUE;
  }

  /* Check if we can nuke it now */
  ai_hunter_try_launch(pplayer, punit, target);
  target = find_unit_by_id(sanity_target);
  if (!target){
    UNIT_LOG(LOGLEVEL_HUNT, punit, "mission accomplished");
    ai_unit_new_role(punit, AIUNIT_NONE, NULL);
    return TRUE;
  }

  /* If we are adjacent - RAMMING SPEED! */
  if (is_tiles_adjacent(punit->tile, target->tile)) {
    ai_unit_attack(punit, target->tile);
    target = find_unit_by_id(sanity_target);
  }

  if (!find_unit_by_id(sanity_own)) {
    return TRUE;
  }
  if (!target) {
    UNIT_LOG(LOGLEVEL_HUNT, punit, "mission accomplished");
    ai_unit_new_role(punit, AIUNIT_NONE, NULL);
  }

  return TRUE;
}
