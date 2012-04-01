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
#ifndef FC__AUTOSETTLERS_H
#define FC__AUTOSETTLERS_H

/* common */
#include "fc_types.h"
#include "map.h"

struct settlermap;
struct pf_path;

void auto_settlers_player(struct player *pplayer);

void auto_settler_findwork(struct player *pplayer, 
                           struct unit *punit,
                           struct settlermap *state,
                           int recursion);

void auto_settler_setup_work(struct player *pplayer, struct unit *punit,
                             struct settlermap *state, int recursion,
                             struct pf_path *path,
                             struct tile *best_tile,
                             enum unit_activity best_act,
                             struct act_tgt *best_target,
                             int completion_time);

int settler_evaluate_improvements(struct unit *punit,
                                  enum unit_activity *best_act,
                                  struct act_tgt *best_target,
                                  struct tile **best_tile,
                                  struct pf_path **path,
                                  struct settlermap *state);

void adv_unit_new_task(struct unit *punit, enum adv_unit_task task,
                       struct tile *ptile);

#endif   /* FC__AUTOSETTLERS_H */
