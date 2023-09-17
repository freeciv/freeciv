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
#ifndef FC__AUTOWORKERS_H
#define FC__AUTOWORKERS_H

/* common */
#include "fc_types.h"

void auto_workers_ruleset_init(void);

struct workermap;
struct pf_path;

void adv_workers_free(void);

void auto_workers_player(struct player *pplayer);

void auto_worker_findwork(const struct civ_map *nmap,
                          struct player *pplayer,
                          struct unit *punit,
                          struct workermap *state,
                          int recursion);

bool auto_worker_setup_work(const struct civ_map *nmap,
                            struct player *pplayer, struct unit *punit,
                            struct workermap *state, int recursion,
                            struct pf_path *path,
                            struct tile *best_tile,
                            enum unit_activity best_act,
                            struct extra_type **best_target,
                            int completion_time);

adv_want worker_evaluate_improvements(const struct civ_map *nmap,
                                      struct unit *punit,
                                      enum unit_activity *best_act,
                                      struct extra_type **best_target,
                                      struct tile **best_tile,
                                      struct pf_path **path,
                                      struct workermap *state);

struct city *worker_evaluate_city_requests(struct unit *punit,
                                           struct worker_task **best_task,
                                           struct pf_path **path,
                                           struct workermap *state);

void adv_unit_new_task(struct unit *punit, enum adv_unit_task task,
                       struct tile *ptile);

bool adv_worker_safe_tile(const struct civ_map *nmap,
                          const struct player *pplayer, struct unit *punit,
                          struct tile *ptile);

adv_want adv_workers_road_bonus(const struct civ_map *nmap,
                                struct tile *ptile, struct road_type *proad);

bool auto_workers_speculate_can_act_at(const struct unit *punit,
                                       enum unit_activity activity,
                                       bool omniscient_cheat,
                                       struct extra_type *target,
                                       const struct tile *ptile);

extern action_id aw_actions_transform[MAX_NUM_ACTIONS];

#define aw_transform_action_iterate(_act_)                                \
{                                                                         \
  action_array_iterate(aw_actions_transform, _act_)

#define aw_transform_action_iterate_end                                   \
  action_array_iterate_end                                                 \
}

extern action_id aw_actions_extra[MAX_NUM_ACTIONS];

#define aw_extra_action_iterate(_act_)                                    \
{                                                                         \
  action_array_iterate(aw_actions_extra, _act_)

#define aw_extra_action_iterate_end                                       \
  action_array_iterate_end                                                \
}

extern action_id aw_actions_rmextra[MAX_NUM_ACTIONS];

#define aw_rmextra_action_iterate(_act_)                                  \
{                                                                         \
  action_array_iterate(aw_actions_rmextra, _act_)

#define aw_rmextra_action_iterate_end                                     \
  action_array_iterate_end                                                \
}

#endif /* FC__AUTOWORKERS_H */
