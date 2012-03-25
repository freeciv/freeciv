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
#include <fc_config.h>
#endif

#include <math.h>
#include <stdio.h>
#include <string.h>

/* utility */
#include "log.h"
#include "mem.h"
#include "support.h"
#include "timing.h"

/* common */
#include "city.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "movement.h"
#include "packets.h"
#include "unitlist.h"

/* common/aicore */
#include "citymap.h"
#include "path_finding.h"
#include "pf_tools.h"

/* server */
#include "citytools.h"
#include "maphand.h"
#include "plrhand.h"
#include "srv_log.h"
#include "unithand.h"
#include "unittools.h"

/* server/advisors */
#include "advdata.h"
#include "advgoto.h"
#include "advtools.h"
#include "infracache.h"

#include "autosettlers.h"

/* This factor is multiplied on when calculating the want.  This is done
 * to avoid rounding errors in comparisons when looking for the best
 * possible work.  However before returning the final want we have to
 * divide by it again.  This loses accuracy but is needed since the want
 * values are used for comparison by the AI in trying to calculate the
 * goodness of building worker units. */
#define WORKER_FACTOR 1024

struct settlermap {
  int enroute; /* unit ID of settler en route to this tile */
  int eta; /* estimated number of turns until enroute arrives */
};

/**************************************************************************
  Calculate the attractiveness of building a road/rail at the given tile.

  This calculates the overall benefit of connecting the civilization; this
  is independent from the local tile (trade) bonus granted by the road.

  "special" must be either S_ROAD or S_RAILROAD.
**************************************************************************/
static int road_bonus(struct tile *ptile, enum tile_special_type special)
{
  int bonus = 0, i;
  bool has_road[12], is_slow[12];
  int dx[12] = {-1,  0,  1, -1, 1, -1, 0, 1,  0, -2, 2, 0};
  int dy[12] = {-1, -1, -1,  0, 0,  1, 1, 1, -2,  0, 0, 2};
  int x, y;

  fc_assert_ret_val(special == S_ROAD || special == S_RAILROAD, 0);

  index_to_map_pos(&x, &y, tile_index(ptile));
  for (i = 0; i < 12; i++) {
    struct tile *tile1 = map_pos_to_tile(x + dx[i], y + dy[i]);

    if (!tile1) {
      has_road[i] = FALSE;
      is_slow[i] = FALSE; /* FIXME: should be TRUE? */
    } else {
      struct terrain *pterrain = tile_terrain(tile1);

      has_road[i] = tile_has_special(tile1, special);

      /* If TRUE, this value indicates that this tile does not need
       * a road connector.  This is set for terrains which cannot have
       * road or where road takes "too long" to build. */
      is_slow[i] = (pterrain->road_time == 0 || pterrain->road_time > 5);

      if (!has_road[i]) {
	unit_list_iterate(tile1->units, punit) {
	  if (punit->activity == ACTIVITY_ROAD 
              || punit->activity == ACTIVITY_RAILROAD) {
	    /* If a road is being built here, consider as if it's already
	     * built. */
	    has_road[i] = TRUE;
          }
	} unit_list_iterate_end;
      }
    }
  }

  /*
   * Consider the following tile arrangement (numbered in hex):
   *
   *   8
   *  012
   * 93 4A
   *  567
   *   B
   *
   * these are the tiles defined by the (dx,dy) arrays above.
   *
   * Then the following algorithm is supposed to determine if it's a good
   * idea to build a road here.  Note this won't work well for hex maps
   * since the (dx,dy) arrays will not cover the same tiles.
   *
   * FIXME: if you can understand the algorithm below please rewrite this
   * explanation!
   */
  if (has_road[0]
      && !has_road[1] && !has_road[3]
      && (!has_road[2] || !has_road[8])
      && (!is_slow[2] || !is_slow[4] || !is_slow[7]
	  || !is_slow[6] || !is_slow[5])) {
    bonus++;
  }
  if (has_road[2]
      && !has_road[1] && !has_road[4]
      && (!has_road[7] || !has_road[10])
      && (!is_slow[0] || !is_slow[3] || !is_slow[7]
	  || !is_slow[6] || !is_slow[5])) {
    bonus++;
  }
  if (has_road[5]
      && !has_road[6] && !has_road[3]
      && (!has_road[5] || !has_road[11])
      && (!is_slow[2] || !is_slow[4] || !is_slow[7]
	  || !is_slow[1] || !is_slow[0])) {
    bonus++;
  }
  if (has_road[7]
      && !has_road[6] && !has_road[4]
      && (!has_road[0] || !has_road[9])
      && (!is_slow[2] || !is_slow[3] || !is_slow[0]
	  || !is_slow[1] || !is_slow[5])) {
    bonus++;
  }

  /*   A
   *  B*B
   *  CCC
   *
   * We are at tile *.  If tile A has a road, and neither B tile does, and
   * one C tile is a valid destination, then we might want a road here.
   *
   * Of course the same logic applies if you rotate the diagram.
   */
  if (has_road[1] && !has_road[4] && !has_road[3]
      && (!is_slow[5] || !is_slow[6] || !is_slow[7])) {
    bonus++;
  }
  if (has_road[3] && !has_road[1] && !has_road[6]
      && (!is_slow[2] || !is_slow[4] || !is_slow[7])) {
    bonus++;
  }
  if (has_road[4] && !has_road[1] && !has_road[6]
      && (!is_slow[0] || !is_slow[3] || !is_slow[5])) {
    bonus++;
  }
  if (has_road[6] && !has_road[4] && !has_road[3]
      && (!is_slow[0] || !is_slow[1] || !is_slow[2])) {
    bonus++;
  }

  return bonus;
}

/****************************************************************************
  Compares the best known tile improvement action with improving ptile
  with activity act.  Calculates the value of improving the tile by
  discounting the total value by the time it would take to do the work
  and multiplying by some factor.
****************************************************************************/
static void consider_settler_action(const struct player *pplayer, 
                                    enum unit_activity act, int extra, 
                                    int new_tile_value, int old_tile_value,
                                    bool in_use, int delay,
                                    int *best_value,
                                    int *best_old_tile_value,
                                    enum unit_activity *best_act,
                                    struct tile **best_tile,
                                    struct tile *ptile)
{
  bool consider;
  int total_value = 0, base_value = 0;
  
  if (extra >= 0) {
    consider = TRUE;
  } else {
    consider = (new_tile_value > old_tile_value);
    extra = 0;
  }

  /* find the present value of the future benefit of this action */
  if (consider) {

    base_value = new_tile_value - old_tile_value;
    total_value = base_value * WORKER_FACTOR;
    if (!in_use) {
      total_value /= 2;
    }
    total_value += extra * WORKER_FACTOR;

    /* use factor to prevent rounding errors */
    total_value = amortize(total_value, delay);
  } else {
    total_value = 0;
  }

  if (total_value > *best_value
      || (total_value == *best_value
	  && old_tile_value > *best_old_tile_value)) {
    log_debug("Replacing (%d, %d) = %d with %s (%d, %d) = %d [d=%d b=%d]",
              TILE_XY(*best_tile), *best_value, get_activity_text(act),
              TILE_XY(ptile), total_value, delay, base_value);
    *best_value = total_value;
    *best_old_tile_value = old_tile_value;
    *best_act = act;
    *best_tile = ptile;
  }
}

/**************************************************************************
  Don't enter in enemy territories.
**************************************************************************/
static bool autosettler_enter_territory(const struct player *pplayer,
                                        const struct tile *ptile)
{
  const struct player *owner = tile_owner(ptile);

  return (NULL == owner
          || pplayers_allied(owner, pplayer));
}

/****************************************************************************
  Finds tiles to improve, using punit.

  The returned value is the goodness of the best tile and action found.  If
  this return value is > 0, then best_tile indicates the tile chosen,
  bestact indicates the activity it wants to do, and path (if not NULL)
  indicates the path to follow for the unit.  If 0 is returned
  then there are no worthwhile activities available.

  completion_time is the time that would be taken by punit to travel to
  and complete work at best_tile

  state contains, for each tile, the unit id of the worker en route,
  and the eta of this worker (if any).  This information
  is used to possibly displace this previously assigned worker.
  if this array is NULL, workers are never displaced.
****************************************************************************/
int settler_evaluate_improvements(struct unit *punit,
                                  enum unit_activity *best_act,
                                  struct tile **best_tile,
                                  struct pf_path **path,
                                  struct settlermap *state)
{
  const struct player *pplayer = unit_owner(punit);
  struct pf_parameter parameter;
  struct pf_map *pfm;
  struct pf_position pos;
  int oldv;             /* Current value of consideration tile. */
  int best_oldv = 9999; /* oldv of best target so far; compared if
                         * newv == best_newv; not initialized to zero,
                         * so that newv = 0 activities are not chosen. */
  int best_newv = 0;

  /* closest worker, if any, headed towards target tile */
  struct unit *enroute = NULL;

  pft_fill_unit_parameter(&parameter, punit);
  parameter.can_invade_tile = autosettler_enter_territory;
  pfm = pf_map_new(&parameter);

  city_list_iterate(pplayer->cities, pcity) {
    struct tile *pcenter = city_tile(pcity);

    /* try to work near the city */
    city_tile_iterate_index(city_map_radius_sq_get(pcity), pcenter, ptile,
                            cindex) {
      bool consider = TRUE;
      bool in_use = (tile_worked(ptile) == pcity);

      if (!in_use && !city_can_work_tile(pcity, ptile)) {
        /* Don't risk bothering with this tile. */
        continue;
      }

      /* Do not go to tiles that already have workers there. */
      unit_list_iterate(ptile->units, aunit) {
        if (unit_owner(aunit) == pplayer
            && aunit->id != punit->id
            && unit_has_type_flag(aunit, F_SETTLERS)) {
          consider = FALSE;
        }
      } unit_list_iterate_end;

      if (!consider) {
        continue;
      }

      if (state) {
        enroute = player_unit_by_number(pplayer,
                                        state[tile_index(ptile)].enroute);
      }

      if (pf_map_position(pfm, ptile, &pos)) {
        int eta = FC_INFINITY, inbound_distance = FC_INFINITY, time;

        if (enroute) {
          eta = state[tile_index(ptile)].eta;
          inbound_distance = real_map_distance(ptile, unit_tile(enroute));
        }

        /* Only consider this tile if we are closer in time and space to
         * it than our other worker (if any) travelling to the site. */
        if ((enroute && enroute->id == punit->id)
            || pos.turn < eta
            || (pos.turn == eta
                && (real_map_distance(ptile, unit_tile(punit))
                    < inbound_distance))) {

          if (enroute) {
            UNIT_LOG(LOG_DEBUG, punit,
                     "Considering (%d, %d) because we're closer "
                     "(%d, %d) than %d (%d, %d)",
                     TILE_XY(ptile), pos.turn,
                     real_map_distance(ptile, unit_tile(punit)),
                     enroute->id, eta, inbound_distance);
          }

          oldv = city_tile_value(pcity, ptile, 0, 0);

          /* Now, consider various activities... */
          activity_type_iterate(act) {
            struct act_tgt target = { .type = ATT_SPECIAL, .obj.spe = S_LAST };

            if (adv_city_worker_act_get(pcity, cindex, act) >= 0
                /* These need separate implementations. */
                && act != ACTIVITY_BASE
                && act != ACTIVITY_GEN_ROAD
                && can_unit_do_activity_targeted_at(punit, act, &target,
                                                    ptile)) {
              int extra = 0;
              int base_value = adv_city_worker_act_get(pcity, cindex, act);

              time = pos.turn + get_turns_for_activity_at(punit, act, ptile);

              if (act == ACTIVITY_ROAD) {
                struct road_type *proad = road_by_special(S_ROAD);
                struct road_type *prail = road_by_special(S_RAILROAD);

                extra = road_bonus(ptile, S_ROAD) * 5;

                if (prail != NULL) {
                  if (!can_build_road(prail, punit, ptile)) {
                    /* Railroad building is not possible without road... */
                    struct tile *virt = tile_virtual_new(ptile);

                    tile_add_road(virt, proad);

                    if (can_build_road(prail, punit, virt)) {
                      /* ... but is possible with road.
                       * Consider making
                       * road here, and set extras and time to to consider
                       * railroads in main consider_settler_action call. */
                      consider_settler_action(pplayer, act, extra, base_value,
                                              oldv, in_use, time,
                                              &best_newv, &best_oldv,
                                              best_act, best_tile, ptile);

                      base_value = adv_city_worker_act_get(pcity, cindex,
                                                           ACTIVITY_RAILROAD);

                      /* Count road time plus rail time. */
                      time += get_turns_for_activity_at(punit, ACTIVITY_RAILROAD, 
                                                        ptile);

                      /* Bonus for rail connectivity instead of road. */
                      extra = road_bonus(ptile, S_RAILROAD) * 3;
                    }

                    tile_virtual_destroy(virt);
                  }
                }
              } else if (act == ACTIVITY_RAILROAD) {
                extra = road_bonus(ptile, S_RAILROAD) * 3;
              } else if (act == ACTIVITY_FALLOUT) {
                extra = pplayer->ai_common.frost;
              } else if (act == ACTIVITY_POLLUTION) {
                extra = pplayer->ai_common.warmth;
              }

              consider_settler_action(pplayer, act, extra, base_value,
                                      oldv, in_use, time,
                                      &best_newv, &best_oldv,
                                      best_act, best_tile, ptile);

            } /* endif: can the worker perform this action */
          } activity_type_iterate_end;
        } /* endif: can we finish sooner than current worker, if any? */
      } /* endif: are we travelling to a legal destination? */
    } city_tile_iterate_index_end;
  } city_list_iterate_end;

  best_newv /= WORKER_FACTOR;

  best_newv = MAX(best_newv, 0); /* sanity */

  if (best_newv > 0) {
    log_debug("Settler %d@(%d,%d) wants to %s at (%d,%d) with desire %d",
              punit->id, TILE_XY(unit_tile(punit)),
              get_activity_text(*best_act), TILE_XY(*best_tile), best_newv);
  } else {
    /* Fill in dummy values.  The callers should check if the return value
     * is > 0 but this will avoid confusing them. */
    *best_act = ACTIVITY_IDLE;
    *best_tile = NULL;
  }

  if (path) {
    *path = *best_tile ? pf_map_path(pfm, *best_tile) : NULL;
  }

  pf_map_destroy(pfm);

  return best_newv;
}

/**************************************************************************
  Find some work for our settlers and/or workers.
**************************************************************************/
#define LOG_SETTLER LOG_DEBUG
void auto_settler_findwork(struct player *pplayer, 
                           struct unit *punit,
                           struct settlermap *state,
                           int recursion)
{
  enum unit_activity best_act;
  struct tile *best_tile = NULL;
  struct pf_path *path = NULL;

  /* time it will take worker to complete its given task */
  int completion_time = 0;

  if (recursion > unit_list_size(pplayer->units)) {
    fc_assert(recursion <= unit_list_size(pplayer->units));
    adv_unit_new_task(punit, AUT_NONE, NULL);
    set_unit_activity(punit, ACTIVITY_IDLE);
    send_unit_info(NULL, punit);
    return; /* avoid further recursion. */
  }

  CHECK_UNIT(punit);

  fc_assert_ret(pplayer && punit);
  fc_assert_ret(unit_has_type_flag(punit, F_CITIES)
                || unit_has_type_flag(punit, F_SETTLERS));

  /*** Try find some work ***/

  if (unit_has_type_flag(punit, F_SETTLERS)) {
    TIMING_LOG(AIT_WORKERS, TIMER_START);
    settler_evaluate_improvements(punit, &best_act, &best_tile, 
                                  &path, state);
    if (path) {
      completion_time = pf_path_last_position(path)->turn;
    }
    TIMING_LOG(AIT_WORKERS, TIMER_STOP);
  }

  adv_unit_new_task(punit, AUT_AUTO_SETTLER, best_tile);

  auto_settler_setup_work(pplayer, punit, state, recursion, path,
                          best_tile, best_act,
                          completion_time);

  if (NULL != path) {
    pf_path_destroy(path);
  }
}

/**************************************************************************
  Setup our settler to do the work it has found
**************************************************************************/
void auto_settler_setup_work(struct player *pplayer, struct unit *punit,
                             struct settlermap *state, int recursion,
                             struct pf_path *path,
                             struct tile *best_tile,
                             enum unit_activity best_act,
                             int completion_time)
{
  /* Run the "autosettler" program */
  if (punit->server.adv->task == AUT_AUTO_SETTLER) {
    struct pf_map *pfm = NULL;
    struct pf_parameter parameter;

    struct unit *displaced;

    if (!best_tile) {
      UNIT_LOG(LOG_DEBUG, punit, "giving up trying to improve terrain");
      return; /* We cannot do anything */
    }

    /* Mark the square as taken. */
    displaced = player_unit_by_number(pplayer,
                                      state[tile_index(best_tile)].enroute);

    if (displaced) {
      fc_assert(state[tile_index(best_tile)].enroute == displaced->id);
      fc_assert(state[tile_index(best_tile)].eta > completion_time
                || (state[tile_index(best_tile)].eta == completion_time
                    && (real_map_distance(best_tile, unit_tile(punit))
                        < real_map_distance(best_tile,
                                            unit_tile(displaced)))));
      UNIT_LOG(LOG_DEBUG, punit,
               "%d (%d,%d) has displaced %d (%d,%d) on %d,%d",
               punit->id, completion_time,
               real_map_distance(best_tile, unit_tile(punit)),
               displaced->id, state[tile_index(best_tile)].eta,
               real_map_distance(best_tile, unit_tile(displaced)),
               TILE_XY(best_tile));
    }

    state[tile_index(best_tile)].enroute = punit->id;
    state[tile_index(best_tile)].eta = completion_time;
      
    if (displaced) {
      struct tile *goto_tile = punit->goto_tile;
      int saved_id = punit->id;
      struct tile *old_pos = unit_tile(punit);

      displaced->goto_tile = NULL;
      auto_settler_findwork(pplayer, displaced, state, recursion + 1);
      if (NULL == player_unit_by_number(pplayer, saved_id)) {
        /* Actions of the displaced settler somehow caused this settler
         * to die. (maybe by recursively giving control back to this unit)
         */
        return;
      }
      if (goto_tile != punit->goto_tile || old_pos != unit_tile(punit)) {
        /* Actions of the displaced settler somehow caused this settler
         * to get a new job, or to already move toward current job.
         * (A displaced B, B displaced C, C displaced A)
         */
        UNIT_LOG(LOG_DEBUG, punit,
                 "%d itself acted due to displacement recursion. "
                 "Was going from (%d, %d) to (%d, %d). "
                 "Now heading from (%d, %d) to (%d, %d).",
                 punit->id,
                 TILE_XY(old_pos), TILE_XY(goto_tile),
                 TILE_XY(unit_tile(punit)), TILE_XY(punit->goto_tile));
        return;
      }
    }

    if (!path) {
      pft_fill_unit_parameter(&parameter, punit);
      parameter.can_invade_tile = autosettler_enter_territory;
      pfm = pf_map_new(&parameter);
      path = pf_map_path(pfm, best_tile);
    }

    if (path) {
      bool alive;

      alive = adv_follow_path(punit, path, best_tile);

      if (alive && same_pos(unit_tile(punit), best_tile)
	  && punit->moves_left > 0) {
	/* Reached destination and can start working immediately */
        unit_activity_handling(punit, best_act);
        send_unit_info(NULL, punit); /* FIXME: probably duplicate */
      }
    } else {
      log_debug("Autosettler does not find path (%d, %d) -> (%d, %d)",
                TILE_XY(unit_tile(punit)), TILE_XY(best_tile));
    }

    if (pfm) {
      pf_map_destroy(pfm);
    }

    return;
  }
}
#undef LOG_SETTLER

/************************************************************************** 
  Run through all the players settlers and let those on ai.control work 
  automagically.
**************************************************************************/
void auto_settlers_player(struct player *pplayer) 
{
  static struct timer *t = NULL;      /* alloc once, never free */
  struct settlermap *state;

  state = fc_calloc(MAP_INDEX_SIZE, sizeof(*state));

  t = renew_timer_start(t, TIMER_CPU, TIMER_DEBUG);

  if (pplayer->ai_controlled) {
    /* Set up our city map. */
    citymap_turn_init(pplayer);
  }

  whole_map_iterate(ptile) {
    state[tile_index(ptile)].enroute = -1;
    state[tile_index(ptile)].eta = FC_INFINITY;    
  } whole_map_iterate_end;

  /* Initialize the infrastructure cache, which is used shortly. */
  initialize_infrastructure_cache(pplayer);

  /* An extra consideration for the benefit of cleaning up pollution/fallout.
   * This depends heavily on the calculations in update_environmental_upset.
   * Aside from that it's more or less a WAG that simply grows incredibly
   * large as an environmental disaster approaches. */
  pplayer->ai_common.warmth
    = (WARMING_FACTOR * game.info.heating / ((game.info.warminglevel + 1) / 2)
       + game.info.globalwarming);
  pplayer->ai_common.frost
    = (COOLING_FACTOR * game.info.cooling / ((game.info.coolinglevel + 1) / 2)
       + game.info.nuclearwinter);

  log_debug("Warmth = %d, game.globalwarming=%d",
            pplayer->ai_common.warmth, game.info.globalwarming);
  log_debug("Frost = %d, game.nuclearwinter=%d",
            pplayer->ai_common.warmth, game.info.nuclearwinter);

  /* Auto-settle with a settler unit if it's under AI control (e.g. human
   * player auto-settler mode) or if the player is an AI.  But don't
   * auto-settle with a unit under orders even for an AI player - these come
   * from the human player and take precedence. */
  unit_list_iterate_safe(pplayer->units, punit) {
    if ((punit->ai_controlled || pplayer->ai_controlled)
        && (unit_has_type_flag(punit, F_SETTLERS)
            || unit_has_type_flag(punit, F_CITIES))
        && !unit_has_orders(punit)
        && punit->moves_left > 0) {
      log_debug("%s settler at (%d, %d) is ai controlled.",
                nation_rule_name(nation_of_player(pplayer)),
                TILE_XY(unit_tile(punit)));
      if (punit->activity == ACTIVITY_SENTRY) {
        unit_activity_handling(punit, ACTIVITY_IDLE);
      }
      if (punit->activity == ACTIVITY_GOTO && punit->moves_left > 0) {
        unit_activity_handling(punit, ACTIVITY_IDLE);
      }
      if (punit->activity == ACTIVITY_IDLE) {
        if (!pplayer->ai_controlled) {
          auto_settler_findwork(pplayer, punit, state, 0);
        } else {
          CALL_PLR_AI_FUNC(settler_run, pplayer, pplayer, punit, state);
        }
      }
    }
  } unit_list_iterate_safe_end;
  /* Reset auto settler state for the next run. */
  if (pplayer->ai_controlled) {
    CALL_PLR_AI_FUNC(settler_reset, pplayer, pplayer);
  }

  if (timer_in_use(t)) {

#ifdef LOG_TIMERS
    log_verbose("%s autosettlers consumed %g milliseconds.",
                nation_rule_name(nation_of_player(pplayer)),
                1000.0 * read_timer_seconds(t));
#else
    log_verbose("%s autosettlers finished",
                nation_rule_name(nation_of_player(pplayer)));
#endif

  }

  FC_FREE(state);
}

/************************************************************************** 
  Change unit's advisor task.
**************************************************************************/
void adv_unit_new_task(struct unit *punit, enum adv_unit_task task,
                       struct tile *ptile)
{
  if (punit->server.adv->task == task) {
    /* Already that task */
    return;
  }

  punit->server.adv->task = task;

  CALL_PLR_AI_FUNC(unit_task, unit_owner(punit), punit, task, ptile);
}
