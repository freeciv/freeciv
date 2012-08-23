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
#include <fc_config.h>
#endif

/* utility */
#include "bitvector.h"
#include "log.h"

/* common */
#include "city.h"
#include "combat.h"
#include "game.h"
#include "map.h"
#include "movement.h"
#include "player.h"
#include "unit.h"
#include "unitlist.h"

/* aicore */
#include "pf_tools.h"

/* server */
#include "citytools.h"
#include "srv_log.h"
#include "unittools.h"

/* server/advisors */
#include "advdata.h"
#include "advgoto.h"
#include "advtools.h"
#include "autosettlers.h"

/* ai */
#include "aicity.h"
#include "aiplayer.h"
#include "aitools.h"
#include "aiunit.h"

#include "aihunt.h"


/**************************************************************************
  We don't need a hunter in this city if we already have one. Return 
  existing hunter if any.
**************************************************************************/
static struct unit *dai_hunter_find(struct player *pplayer, 
                                    struct city *pcity)
{
  unit_list_iterate(pcity->units_supported, punit) {
    if (dai_hunter_qualify(pplayer, punit)) {
      return punit;
    }
  } unit_list_iterate_end;
  unit_list_iterate(pcity->tile->units, punit) {
    if (dai_hunter_qualify(pplayer, punit)) {
      return punit;
    }
  } unit_list_iterate_end;

  return NULL;
}

/**************************************************************************
  Guess best hunter unit type.
**************************************************************************/
static struct unit_type *dai_hunter_guess_best(struct city *pcity,
                                               enum unit_move_type umt)
{
  struct unit_type *bestid = NULL;
  int best = 0;

  unit_type_iterate(ut) {
    int desire;

    if (utype_move_type(ut) != umt
     || !can_city_build_unit_now(pcity, ut)
     || ut->attack_strength < ut->transport_capacity) {
      continue;
    }

    desire = (ut->hp
              * ut->attack_strength
              * ut->firepower
              * ut->move_rate
              + ut->defense_strength) / MAX(UNITTYPE_COSTS(ut), 1);

    unit_class_iterate(uclass) {
      if (can_unit_type_transport(ut, uclass)
          && uclass_has_flag(uclass, UCF_MISSILE)) {
        desire += desire / 6;
        break;
      }
    } unit_class_iterate_end;

    if (utype_has_flag(ut, UTYF_IGTER)) {
      desire += desire / 2;
    }
    if (utype_has_flag(ut, UTYF_PARTIAL_INVIS)) {
      desire += desire / 4;
    }
    if (!can_attack_non_native(ut)) {
      desire -= desire / 4; /* less flexibility */
    }
    /* Causes continual unhappiness */
    if (utype_has_flag(ut, UTYF_FIELDUNIT)) {
      desire /= 2;
    }

    desire = amortize(desire,
		      (utype_build_shield_cost(ut)
		       / MAX(pcity->surplus[O_SHIELD], 1)));

    if (desire > best) {
        best = desire;
        bestid = ut;
    }
  } unit_type_iterate_end;

  return bestid;
}

/**************************************************************************
  Check if we want to build a missile for our hunter.
**************************************************************************/
static void dai_hunter_missile_want(struct player *pplayer,
                                    struct city *pcity,
                                    struct adv_choice *choice)
{
  int best = -1;
  struct unit_type *best_unit_type = NULL;
  struct unit *hunter = NULL;

  unit_list_iterate(pcity->tile->units, punit) {
    if (dai_hunter_qualify(pplayer, punit)) {
      unit_class_iterate(uclass) {
        if (can_unit_type_transport(unit_type(punit), uclass)
            && uclass_has_flag(uclass, UCF_MISSILE)) {
          hunter = punit;
          break;
        }
      } unit_class_iterate_end;
      if (hunter) {
        break;
      }
    }
  } unit_list_iterate_end;

  if (!hunter) {
    return;
  }

  unit_type_iterate(ut) {
    int desire;

    if (!uclass_has_flag(utype_class(ut), UCF_MISSILE)
     || !can_city_build_unit_now(pcity, ut)) {
      continue;
    }

    if (!can_unit_type_transport(unit_type(hunter), utype_class(ut))) {
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
    if (utype_has_flag(ut, UTYF_FIELDUNIT)) {
      desire /= 2;
    }

    desire = amortize(desire,
		      (utype_build_shield_cost(ut)
		       / MAX(pcity->surplus[O_SHIELD], 1)));

    if (desire > best) {
        best = desire;
        best_unit_type = ut;
    }
  } unit_type_iterate_end;

  if (best > choice->want) {
    CITY_LOG(LOGLEVEL_HUNT, pcity, "pri missile w/ want %d", best);
    choice->value.utype = best_unit_type;
    choice->want = best;
    choice->type = CT_ATTACKER;
    choice->need_boat = FALSE;
  } else if (best != -1) {
    CITY_LOG(LOGLEVEL_HUNT, pcity, "not pri missile w/ want %d"
             "(old want %d)", best, choice->want);
  }
}

/**************************************************************************
  Support function for ai_hunter_choice()
**************************************************************************/
static void eval_hunter_want(struct ai_type *ait, struct player *pplayer,
                             struct city *pcity,
                             struct adv_choice *choice,
			     struct unit_type *best_type,
                             int veteran)
{
  struct unit *virtualunit;
  int want = 0;

  virtualunit = unit_virtual_create(pplayer, pcity, best_type, veteran);
  want = dai_hunter_manage(ait, pplayer, virtualunit);
  unit_virtual_destroy(virtualunit);
  if (want > choice->want) {
    CITY_LOG(LOGLEVEL_HUNT, pcity, "pri hunter w/ want %d", want);
    choice->value.utype = best_type;
    choice->want = want;
    choice->type = CT_ATTACKER;
    choice->need_boat = FALSE;
  }
}

/**************************************************************************
  Check if we want to build a hunter.
**************************************************************************/
void dai_hunter_choice(struct ai_type *ait, struct player *pplayer,
                       struct city *pcity, struct adv_choice *choice)
{
  struct unit_type *best_land_hunter
    = dai_hunter_guess_best(pcity, UMT_LAND);
  struct unit_type *best_sea_hunter
    = dai_hunter_guess_best(pcity, UMT_SEA);
  struct unit *hunter = dai_hunter_find(pplayer, pcity);

  if ((!best_land_hunter && !best_sea_hunter)
      || is_barbarian(pplayer) || !pplayer->is_alive
      || ai_handicap(pplayer, H_TARGETS)) {
    return; /* None available */
  }
  if (hunter) {
    /* Maybe want missiles to go with a hunter instead? */
    dai_hunter_missile_want(pplayer, pcity, choice);
    return;
  }

  if (best_sea_hunter) {
    eval_hunter_want(ait, pplayer, pcity, choice, best_sea_hunter, 
                     do_make_unit_veteran(pcity, best_sea_hunter));
  }
  if (best_land_hunter) {
    eval_hunter_want(ait, pplayer, pcity, choice, best_land_hunter, 
                     do_make_unit_veteran(pcity, best_land_hunter));
  }
}

/**************************************************************************
  Does this unit qualify as a hunter?
**************************************************************************/
bool dai_hunter_qualify(struct player *pplayer, struct unit *punit)
{
  if (is_barbarian(pplayer) || unit_owner(punit) != pplayer) {
    return FALSE;
  }
  if (unit_has_type_role(punit, L_HUNTER)) {
    return TRUE;
  }
  return FALSE;
}

/**************************************************************************
  Try to shoot our target with a missile. Also shoot down anything that
  might attempt to intercept _us_. We assign missiles to a hunter in
  ai_unit_new_role().
**************************************************************************/
static void dai_hunter_try_launch(struct ai_type *ait,
                                  struct player *pplayer,
                                  struct unit *punit,
                                  struct unit *target)
{
  int target_sanity = target->id;
  struct pf_parameter parameter;
  struct pf_map *pfm;

  unit_list_iterate_safe(unit_tile(punit)->units, missile) {
    struct unit *sucker = NULL;

    if (unit_owner(missile) == pplayer
        && uclass_has_flag(unit_class(missile), UCF_MISSILE)) {
      UNIT_LOG(LOGLEVEL_HUNT, missile, "checking for hunt targets");
      pft_fill_unit_parameter(&parameter, punit);
      pfm = pf_map_new(&parameter);

      pf_map_move_costs_iterate(pfm, ptile, move_cost, FALSE) {
        if (move_cost > missile->moves_left / SINGLE_MOVE) {
          break;
        }
        if (tile_city(ptile) || !can_unit_attack_tile(punit, ptile)) {
          continue;
        }
        unit_list_iterate(ptile->units, victim) {
          struct unit_type *ut = unit_type(victim);
          enum diplstate_type ds =
	    player_diplstate_get(pplayer, unit_owner(victim))->type;

          if (ds != DS_WAR) {
            continue;
          }
          if (victim == target) {
            sucker = victim;
            UNIT_LOG(LOGLEVEL_HUNT, missile, "found primary target %d(%d, %d)"
                     " dist %d", victim->id, TILE_XY(unit_tile(victim)),
                     move_cost);
            break; /* Our target! Get him!!! */
          }
          if (ut->move_rate + victim->moves_left > move_cost
              && ATTACK_POWER(victim) > DEFENCE_POWER(punit)
              && (utype_move_type(ut) == UMT_SEA
                  || utype_move_type(ut) == UMT_BOTH)) {
            /* Threat to our carrier. Kill it. */
            sucker = victim;
            UNIT_LOG(LOGLEVEL_HUNT, missile, "found aux target %d(%d, %d)",
                     victim->id, TILE_XY(unit_tile(victim)));
            break;
          }
        } unit_list_iterate_end;
        if (sucker) {
          break; /* found something - kill it! */
        }
      } pf_map_move_costs_iterate_end;
      pf_map_destroy(pfm);
      if (sucker) {
        if (unit_transported(missile)) {
          unit_transport_unload_send(missile);
        }
        missile->goto_tile = unit_tile(sucker);
        if (dai_unit_goto(ait, missile, unit_tile(sucker))) {
          /* We survived; did they? */
          sucker = game_unit_by_number(target_sanity); /* Sanity */
          if (sucker && is_tiles_adjacent(unit_tile(sucker),
                                          unit_tile(missile))) {
            dai_unit_attack(ait, missile, unit_tile(sucker));
          }
        }
        target = game_unit_by_number(target_sanity); /* Sanity */
        break; /* try next missile, if any */
      }
    } /* if */
  } unit_list_iterate_safe_end;
}

/**************************************************************************
  Calculate desire to crush this target.
**************************************************************************/
static void dai_hunter_juiciness(struct player *pplayer, struct unit *punit,
                                 struct unit *target, int *stackthreat,
                                 int *stackcost)
{
  *stackthreat = 0;
  *stackcost = 0;

  unit_list_iterate(unit_tile(target)->units, sucker) {
    *stackthreat += ATTACK_POWER(sucker);
    if (unit_has_type_flag(sucker, UTYF_GAMELOSS)) {
      *stackcost += 1000;
      *stackthreat += 5000;
    }
    if (unit_has_type_flag(sucker, UTYF_DIPLOMAT)) {
      *stackthreat += 500; /* extra threatening */
    }
    *stackcost += unit_build_shield_cost(sucker);
  } unit_list_iterate_end;

  *stackthreat *= 9; /* WAG - reduced by distance later */
  *stackthreat += *stackcost;
}

/**************************************************************************
  Manage a (possibly virtual) hunter. Return the want for building a 
  hunter like this. If we return 0, then we have nothing to do with
  the hunter. If we return -1, then we succeeded, and can try again.
  If we return > 0 then we are hunting but ran out of moves (this is
  also used for construction want).

  We try to keep track of our original target, but also opportunistically
  snatch up closer targts if they are better.

  We set punit->server.ai->target to target's id.
**************************************************************************/
int dai_hunter_manage(struct ai_type *ait, struct player *pplayer,
                      struct unit *punit)
{
  bool is_virtual = (punit->id == 0);
  struct pf_parameter parameter;
  struct pf_map *pfm;
  int limit = unit_move_rate(punit) * 6;
  struct unit_ai *unit_data = def_ai_unit_data(punit, ait);
  struct unit *original_target = game_unit_by_number(unit_data->target);
  int original_threat = 0, original_cost = 0;

  fc_assert_ret_val(!is_barbarian(pplayer), 0);
  fc_assert_ret_val(pplayer->is_alive, 0);

  pft_fill_unit_parameter(&parameter, punit);
  pfm = pf_map_new(&parameter);

  if (original_target) {
    dai_hunter_juiciness(pplayer, punit, original_target, 
                         &original_threat, &original_cost);
  }

  pf_map_move_costs_iterate(pfm, ptile, move_cost, FALSE) {
    /* End faster if we have a target */
    if (move_cost > limit) {
      UNIT_LOG(LOGLEVEL_HUNT, punit, "gave up finding hunt target");
      pf_map_destroy(pfm);
      return 0;
    }

    if (tile_city(ptile) || !can_unit_attack_tile(punit, ptile)) {
      continue;
    }

    unit_list_iterate_safe(ptile->units, target) {
      struct player *aplayer = unit_owner(target);
      int dist1, dist2, stackthreat = 0, stackcost = 0;
      int sanity_target = target->id;
      struct pf_path *path;
      struct unit_ai *target_data;

      /* Note that we need not (yet) be at war with aplayer */
      if (!adv_is_player_dangerous(pplayer, aplayer)) {
        continue;
      }

      target_data = def_ai_unit_data(target, ait);
      if (BV_ISSET(target_data->hunted, player_index(pplayer))) {
        /* Can't hunt this one.  The bit is cleared in the beginning
         * of each turn. */
        continue;
      }
      if (!unit_has_type_flag(target, UTYF_DIPLOMAT)
          && get_transporter_capacity(target) == 0
          && !unit_has_type_flag(target, UTYF_GAMELOSS)) {
        /* Won't hunt this one. */
        continue;
      }

      /* Figure out whether unit is coming closer */
      if (target_data->cur_pos && target_data->prev_pos) {
        dist1 = real_map_distance(unit_tile(punit), *target_data->cur_pos);
        dist2 = real_map_distance(unit_tile(punit), *target_data->prev_pos);
      } else {
        dist1 = dist2 = 0;
      }
      UNIT_LOG(LOGLEVEL_HUNT, punit, "considering chasing %s[%d](%d, %d) "
                                     "dist1 %d dist2 %d",
               unit_rule_name(target), target->id, TILE_XY(unit_tile(target)),
               dist1, dist2);

      /* We can't chase if we aren't faster or on intercept vector */
      if (unit_type(punit)->move_rate < unit_type(target)->move_rate
          && dist1 >= dist2) {
        UNIT_LOG(LOGLEVEL_HUNT, punit,
                 "giving up racing %s (%d, %d)->(%d, %d)",
                 unit_rule_name(target),
                 target_data->prev_pos
                   ? index_to_map_pos_x(tile_index(*target_data->prev_pos))
                   : -1,
                 target_data->prev_pos
                   ? index_to_map_pos_y(tile_index(*target_data->prev_pos))
                   : -1,
                 TILE_XY(unit_tile(target)));
        continue;
      }

      /* Calculate juiciness of target, compare with existing target,
       * if any. */
      dai_hunter_juiciness(pplayer, punit, target, &stackthreat, &stackcost);
      stackcost *= unit_win_chance(punit, get_defender(punit,
                                                       unit_tile(target)));
      if (stackcost < unit_build_shield_cost(punit)) {
        UNIT_LOG(LOGLEVEL_HUNT, punit, "%d is too expensive (it %d vs us %d)",
                 target->id, stackcost,
		 unit_build_shield_cost(punit));
        continue; /* Too expensive */
      }
      stackthreat /= move_cost + 1;
      if (!is_virtual 
          && original_target != target
          && original_threat > stackthreat) {
        UNIT_LOG(LOGLEVEL_HUNT, punit, "Unit %d is not worse than %d", 
                 target->id, original_target->id);
        continue; /* The threat we found originally was worse than this! */
      }
      if (stackthreat < unit_build_shield_cost(punit)) {
        UNIT_LOG(LOGLEVEL_HUNT, punit, "%d is not worth it", target->id);
        continue; /* Not worth it */
      }

      UNIT_LOG(LOGLEVEL_HUNT, punit, "hunting %s %s[%d](%d,%d) "
               "with want %d, dist1 %d, dist2 %d", 
               nation_rule_name(nation_of_unit(target)),
               unit_rule_name(target), 
               target->id,
               TILE_XY(unit_tile(target)),
               stackthreat,
               dist1,
               dist2);
      /* Ok, now we FINALLY have a target worth destroying! */
      unit_data->target = target->id;
      if (is_virtual) {
        pf_map_destroy(pfm);
        return stackthreat;
      }

      /* This assigns missiles to us */
      dai_unit_new_task(ait, punit, AIUNIT_HUNTER, unit_tile(target));

      /* Check if we can nuke it */
      dai_hunter_try_launch(ait, pplayer, punit, target);

      /* Check if we have nuked it */
      if (target != game_unit_by_number(sanity_target)) {
        UNIT_LOG(LOGLEVEL_HUNT, punit, "mission accomplished by cargo (pre)");
        dai_unit_new_task(ait, punit, AIUNIT_NONE, NULL);
        pf_map_destroy(pfm);
        return -1; /* try again */
      }

      /* Go towards it. */
      path = pf_map_path(pfm, unit_tile(target));
      if (!adv_unit_execute_path(punit, path)) {
        pf_path_destroy(path);
        pf_map_destroy(pfm);
        return 0;
      }
      pf_path_destroy(path);

      if (target != game_unit_by_number(sanity_target)) {
        UNIT_LOG(LOGLEVEL_HUNT, punit, "mission accomplished");
        dai_unit_new_task(ait, punit, AIUNIT_NONE, NULL);
        pf_map_destroy(pfm);
        return -1; /* try again */
      }

      /* Check if we can nuke it now */
      dai_hunter_try_launch(ait, pplayer, punit, target);
      if (target != game_unit_by_number(sanity_target)) {
        UNIT_LOG(LOGLEVEL_HUNT, punit, "mission accomplished by cargo (post)");
        dai_unit_new_task(ait, punit, AIUNIT_NONE, NULL);
        pf_map_destroy(pfm);
        return -1; /* try again */
      }

      pf_map_destroy(pfm);
      unit_data->done = TRUE;
      return stackthreat; /* still have work to do */
    } unit_list_iterate_safe_end;
  } pf_map_move_costs_iterate_end;

  UNIT_LOG(LOGLEVEL_HUNT, punit, "ran out of map finding hunt target");
  pf_map_destroy(pfm);
  return 0; /* found nothing */
}
