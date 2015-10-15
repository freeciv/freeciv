/**********************************************************************
 Freeciv - Copyright (C) 1996-2015 - Freeciv Development Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__ACTIONTOOLS_H
#define FC__ACTIONTOOLS_H

/* common */
#include "player.h"
#include "tile.h"

void action_consequence_caught(const int action_id,
                               struct player *offender,
                               struct player *victim_player,
                               const struct tile *victim_tile,
                               const char *victim_link);

void action_consequence_success(const int action_id,
                                struct player *offender,
                                struct player *victim_player,
                                const struct tile *victim_tile,
                                const char *victim_link);

struct city *action_tgt_city(struct unit *actor, struct tile *target_tile,
                             bool accept_all_actions);

struct unit *action_tgt_unit(struct unit *actor, struct tile *target_tile,
                             bool accept_all_actions);

#endif /* FC__ACTIONTOOLS_H */
