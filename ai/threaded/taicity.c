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

/* utility */
#include "log.h"

/* common */
#include "city.h"
#include "game.h"
#include "map.h"
#include "movement.h"
#include "tile.h"
#include "unit.h"
#include "workertask.h"

/* server */
#include "citytools.h"

/* server/advisors */
#include "autosettlers.h"
#include "infracache.h"

/* ai/threaded */
#include "taimsg.h"

#include "taicity.h"

/* Task Want Multiplier */
#define TWMP 100

struct tai_worker_task_req
{
  int city_id;
  struct worker_task task;
};

enum tai_worker_task_limitation {
  TWTL_CURRENT_UNITS
};

static bool tai_city_worker_task_select(struct player *pplayer, struct city *pcity,
                                        struct worker_task *task,
                                        enum tai_worker_task_limitation limit);

/**************************************************************************
  Create worker request for the city. Only tasks that existing units can
  do are created.
**************************************************************************/
void tai_city_worker_requests_create(struct player *pplayer, struct city *pcity)
{
  struct worker_task task;

  if (tai_city_worker_task_select(pplayer, pcity, &task, TWTL_CURRENT_UNITS)) {
    struct tai_worker_task_req *data = fc_malloc(sizeof(*data));

    data->city_id = pcity->id;
    data->task.ptile = task.ptile;
    data->task.act = task.act;
    data->task.tgt = task.tgt;
    data->task.want = task.want;

    tai_send_req(TAI_REQ_WORKER_TASK, pplayer, data);
  }
}

/**************************************************************************
  Select worker task suitable for the city.

  TODO: This largely duplicates settler_evaluate_improvements(). Should
        maybe find a way to reuse parts in some common function. OTOH
        though this has started as almost-copy of
        settler_evaluate_improvements(), this is likely to turn very different
        function as part of threaded ai with more CPU to burn.
**************************************************************************/
static bool tai_city_worker_task_select(struct player *pplayer, struct city *pcity,
                                        struct worker_task *task,
                                        enum tai_worker_task_limitation limit)
{
  struct worker_task *selected;
  struct worker_task worked = { .ptile = NULL, .want = 0, .act = ACTIVITY_IDLE, .tgt = NULL };
  struct worker_task unworked = { .ptile = NULL, .want = 0, .act = ACTIVITY_IDLE, .tgt = NULL };
  int uw_max = 0;
  int worst_worked = FC_INFINITY;
  int old_worst_worked = FC_INFINITY;
  int orig_worst_worked = 0;
  struct unit_list *units = NULL;

  switch (limit) {
  case TWTL_CURRENT_UNITS:
    units = pplayer->units;
    break;
  }

  city_tile_iterate_index(city_map_radius_sq_get(pcity), city_tile(pcity),
                          ptile, cindex) {
    int orig_value;
    bool potential_worst_worked = FALSE;

    if (!city_can_work_tile(pcity, ptile)) {
      continue;
    }

    orig_value = city_tile_value(pcity, ptile, 0, 0);

    if (tile_worked(ptile) == pcity
        && orig_value < worst_worked) {
      worst_worked = orig_value;
      orig_worst_worked = orig_value;
      potential_worst_worked = TRUE;
    }

    as_transform_activity_iterate(act) {
      bool consider = TRUE;
      bool possible = FALSE;
      enum extra_cause cause;
      enum extra_rmcause rmcause;

      /* Do not request activities that already are under way. */
      unit_list_iterate(ptile->units, punit) {
        if (unit_owner(punit) == pplayer
            && unit_has_type_flag(punit, UTYF_SETTLERS)
            && punit->activity == act) {
          consider = FALSE;
          break;
        }
      } unit_list_iterate_end;

      if (!consider) {
        continue;
      }

      cause = activity_to_extra_cause(act);
      rmcause = activity_to_extra_rmcause(act);

      unit_list_iterate(units, punit) {
        struct extra_type *tgt = NULL;

        if (cause != EC_NONE) {
          tgt = next_extra_for_tile(ptile, cause, pplayer, punit);
        } else if (rmcause != ERM_NONE) {
          tgt = prev_extra_in_tile(ptile, rmcause, pplayer, punit);
        }

        if (can_unit_do_activity_targeted_at(punit, act, tgt, ptile)) {
          possible = TRUE;
          break;
        }
      } unit_list_iterate_end;

      if (possible) {
        int value = adv_city_worker_act_get(pcity, cindex, act);

        if (tile_worked(ptile) == pcity) {
          if ((value - orig_value) * TWMP > worked.want) {
            worked.want  = TWMP * (value - orig_value);
            worked.ptile = ptile;
            worked.act   = act;
            worked.tgt   = NULL;
          }
          if (value > old_worst_worked) {
            /* After improvement it would not be the worst */
            potential_worst_worked = FALSE;
          } else {
            worst_worked = value;
          }
        } else {
          if (value > orig_value && value > uw_max) {
            uw_max = value;
            unworked.want  = TWMP * (value - orig_value);
            unworked.ptile = ptile;
            unworked.act   = act;
            unworked.tgt   = NULL;
          }
        }
      }
    } as_transform_activity_iterate_end;

    extra_type_iterate(tgt) {
      enum unit_activity act = ACTIVITY_LAST;
      bool removing = tile_has_extra(ptile, tgt);

      unit_list_iterate(units, punit) {
        if (removing) {
          as_rmextra_activity_iterate(try_act) {
            if (is_extra_removed_by_action(tgt, try_act)
                && can_unit_do_activity_targeted_at(punit, try_act, tgt, ptile)) {
              act = try_act;
              break;
            }
          } as_rmextra_activity_iterate_end;
        } else {
          as_extra_activity_iterate(try_act) {
            if (is_extra_caused_by_action(tgt, try_act)
                && can_unit_do_activity_targeted_at(punit, try_act, tgt, ptile)) {
              act = try_act;
              break;
            }
          } as_extra_activity_iterate_end;
        }
      } unit_list_iterate_end;

      if (act != ACTIVITY_LAST) {
        int value;
        int extra;
        bool consider = TRUE;
        struct road_type *proad;

        /* Do not request activities that already are under way. */
        unit_list_iterate(ptile->units, punit) {
          if (unit_owner(punit) == pplayer
              && unit_has_type_flag(punit, UTYF_SETTLERS)
              && punit->activity == act) {
            consider = FALSE;
            break;
          }
        } unit_list_iterate_end;

        if (!consider) {
          continue;
        }

        proad = extra_road_get(tgt);

        value = adv_city_worker_extra_get(pcity, cindex, tgt);

        if (proad != NULL && road_provides_move_bonus(proad)) {
          int old_move_cost;
          int mc_multiplier = 1;
          int mc_divisor = 1;

          /* Here 'old' means actually 'without the evaluated': In case of
           * removal activity it's the value after the removal. */
          old_move_cost = tile_terrain(ptile)->movement_cost * SINGLE_MOVE;

          road_type_iterate(pold) {
            if (tile_has_road(ptile, pold) && pold != proad) {
              if (road_provides_move_bonus(pold)
                  && pold->move_cost < old_move_cost) {
                old_move_cost = pold->move_cost;
              }
            }
          } road_type_iterate_end;

          if (proad->move_cost < old_move_cost) {
            if (proad->move_cost >= terrain_control.move_fragments) {
              mc_divisor = proad->move_cost / terrain_control.move_fragments;
            } else {
              if (proad->move_cost == 0) {
                mc_multiplier = 2;
              } else {
                mc_multiplier = 1 - proad->move_cost;
              }
              mc_multiplier += old_move_cost;
            }
          }

          extra = adv_settlers_road_bonus(ptile, proad) * mc_multiplier / mc_divisor;

          if (removing) {
            extra = -extra;
          }
        } else {
          extra = 0;
        }

        value += extra;

        if (tile_worked(ptile) == pcity) {
          if ((value - orig_value) * TWMP > worked.want) {
            worked.want  = TWMP * (value - orig_value);
            worked.ptile = ptile;
            worked.act   = act;
            worked.tgt   = tgt;
          }
          if (value > old_worst_worked) {
            /* After improvement it would not be the worst */
            potential_worst_worked = FALSE;
          } else {
            worst_worked = value;
          }
        } else {
          if (value > orig_value && value > uw_max) {
            uw_max = value;
            unworked.want  = TWMP * (value - orig_value);
            unworked.ptile = ptile;
            unworked.act   = act;
            unworked.tgt   = tgt;
          }
        }
      }
    } extra_type_iterate_end;

    if (potential_worst_worked) {
      /* Would still be worst worked even if we improved *it*. */
      old_worst_worked = worst_worked;
    }
  } city_tile_iterate_end;

  if (worked.ptile == NULL
      || (old_worst_worked < uw_max && (uw_max - orig_worst_worked) * TWMP > worked.want)) {
    /* It's better to improve best yet unworked tile and take it to use after that,
       than to improve already worked tile. */
    selected = &unworked;
  } else {
    selected = &worked;
  }

  if (selected->ptile != NULL) {
    struct extra_type *target = NULL;

    if (selected->tgt == NULL) {      
      enum extra_cause cause = activity_to_extra_cause(selected->act);

      if (cause != EC_NONE) {
        target = next_extra_for_tile(selected->ptile, cause, pplayer, NULL);
      } else {
        enum extra_rmcause rmcause = activity_to_extra_rmcause(selected->act);

        if (rmcause != ERM_NONE) {
          target = prev_extra_in_tile(selected->ptile, rmcause, pplayer, NULL);
        }
      }
    } else {
      target = selected->tgt;
    }

    task->ptile = selected->ptile;
    task->act = selected->act;
    task->tgt = target;
    task->want = selected->want;

    return TRUE;
  }

  return FALSE;
}

/**************************************************************************
  Receive message from thread to main thread.
**************************************************************************/
void tai_req_worker_task_rcv(struct tai_req *req)
{
  struct tai_worker_task_req *data = (struct tai_worker_task_req *)req->data;
  struct city *pcity;

  pcity = game_city_by_number(data->city_id);

  if (pcity != NULL && city_owner(pcity) == req->plr) {
    /* City has not been lost meanwhile */

    log_debug("%s storing req for act %d at (%d,%d)",
              pcity->name, data->task.act, TILE_XY(data->task.ptile));
    pcity->task_req.ptile = data->task.ptile;
    pcity->task_req.act   = data->task.act;
    pcity->task_req.tgt   = data->task.tgt;
    pcity->task_req.want  = data->task.want;

    /* Send info to observers */
    package_and_send_worker_task(pcity);

    free(data);
  }
}
