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

/* server/advisors */
#include "autosettlers.h"
#include "infracache.h"

/* ai/threaded */
#include "taimsg.h"

#include "taicity.h"

struct tai_worker_task_req
{
  int city_id;
  struct worker_task task;
};

/**************************************************************************
  Create worker request for the city.

  TODO: This largely duplicates settler_evaluate_improvements(). Should
        maybe find a way to reuse parts in some common function. OTOH
        though this has started as almost-copy of
        settler_evaluate_improvements(), this is likely to turn very different
        function as part of threaded ai with more CPU to burn.
**************************************************************************/
void tai_city_worker_requests_create(struct player *pplayer, struct city *pcity)
{
  struct worker_task task;

  task.ptile = NULL;
  task.want = 0;

  city_tile_iterate_index(city_map_radius_sq_get(pcity), city_tile(pcity),
                          ptile, cindex) {
    bool consider = TRUE;
    int orig_value;

    if (!city_can_work_tile(pcity, ptile)) {
      continue;
    }

    /* Do not go to tiles that already have workers there. */
    unit_list_iterate(ptile->units, aunit) {
      if (unit_owner(aunit) == pplayer
          && unit_has_type_flag(aunit, UTYF_SETTLERS)) {
        consider = FALSE;
        break;
      }
    } unit_list_iterate_end;

    if (!consider) {
      continue;
    }

    orig_value = city_tile_value(pcity, ptile, 0, 0);

    activity_type_iterate(act) {
      if (act == ACTIVITY_IRRIGATE
          || act == ACTIVITY_MINE
          || act == ACTIVITY_POLLUTION
          || act == ACTIVITY_FALLOUT) {
        int value = adv_city_worker_act_get(pcity, cindex, act);

        if (value - orig_value > task.want) {
          task.want  = value - orig_value;
          task.ptile = ptile;
          task.act   = act;
          task.tgt.type = ATT_SPECIAL;
          task.tgt.obj.spe = S_LAST;
        }
      }
    } activity_type_iterate_end;

    road_type_iterate(proad) {
      int value;
      int old_move_cost;
      int mc_multiplier = 1;
      int mc_divisor = 1;
      int extra;

      if (!player_can_build_road(proad, pplayer, ptile)) {
        continue;
      }

      value = adv_city_worker_road_get(pcity, cindex, proad);
      old_move_cost = tile_terrain(ptile)->movement_cost * SINGLE_MOVE;

      road_type_iterate(pold) {
        if (tile_has_road(ptile, pold)) {
          if (pold->move_mode != RMM_NO_BONUS
              && pold->move_cost < old_move_cost) {
            old_move_cost = pold->move_cost;
          }
        }
      } road_type_iterate_end;

      if (proad->move_cost < old_move_cost) {
        if (proad->move_cost >= 3) {
          mc_divisor = proad->move_cost / 3;
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
      value += extra;

      if (value - orig_value > task.want) {
        task.want  = value - orig_value;
        task.ptile = ptile;
        task.act   = ACTIVITY_GEN_ROAD;
        task.tgt.type = ATT_ROAD;
        task.tgt.obj.road = road_number(proad);
      }
    } road_type_iterate_end;

    base_type_iterate(pbase) {
      int value;

      if (!player_can_build_base(pbase, pplayer, ptile)) {
        continue;
      }

      value = adv_city_worker_base_get(pcity, cindex, pbase);

      if (value - orig_value > task.want) {
        task.want  = value - orig_value;
        task.ptile = ptile;
        task.act   = ACTIVITY_BASE;
        task.tgt.type = ATT_BASE;
        task.tgt.obj.road = base_number(pbase);
      }
    } base_type_iterate_end;
  } city_tile_iterate_end;

  if (task.ptile != NULL) {
    struct tai_worker_task_req *data = fc_malloc(sizeof(*data));

    log_debug("%s: act %d at (%d,%d)", pcity->name, task.act,
              TILE_XY(task.ptile));

    data->city_id = pcity->id;
    data->task.ptile = task.ptile;
    data->task.act = task.act;
    data->task.tgt = task.tgt;

    tai_send_req(TAI_REQ_WORKER_TASK, pplayer, data);
  }
}

/**************************************************************************
  Receive message from thread to main thread.
**************************************************************************/
void tai_req_worker_task_rcv(struct tai_req *req)
{
  struct tai_worker_task_req *data = (struct tai_worker_task_req *)req->data;
  struct city *pcity;

  pcity = game_city_by_number(data->city_id);

  if (pcity != NULL) {
    /* City has not disappeared meanwhile */

    log_debug("%s storing req for act %d at (%d,%d)",
              pcity->name, data->task.act, TILE_XY(data->task.ptile));
    pcity->server.task_req.ptile = data->task.ptile;
    pcity->server.task_req.act   = data->task.act;
    pcity->server.task_req.tgt   = data->task.tgt;
  }
}
