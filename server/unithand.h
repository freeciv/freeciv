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
#ifndef FC__UNITHAND_H
#define FC__UNITHAND_H

/* common */
#include "explanation.h"
#include "unit.h"

/* server */
#include <hand_gen.h>       /* <> so looked from the build directory first. */

bool unit_activity_handling(struct unit *punit,
                            enum unit_activity new_activity,
                            enum gen_action trigger_action);
bool unit_activity_handling_targeted(struct unit *punit,
                                     enum unit_activity new_activity,
                                     struct extra_type **new_target,
                                     enum gen_action trigger_action);
void unit_change_homecity_handling(struct unit *punit, struct city *new_pcity,
                                   bool rehome);

bool unit_move_handling(struct unit *punit, struct tile *pdesttile,
                        bool move_diplomat_city);

void unit_do_action(struct player *pplayer,
                    const int actor_id,
                    const int target_id,
                    const int sub_tgt_id,
                    const char *name,
                    const action_id action_type);

bool unit_perform_action(struct player *pplayer,
                         const int actor_id,
                         const int target_id,
                         const int sub_tgt_id,
                         const char *name,
                         const action_id action_type,
                         const enum action_requester requester);

void illegal_action_msg(struct player *pplayer,
                        const enum event_type event,
                        struct unit *actor,
                        const action_id stopped_action,
                        const struct tile *target_tile,
                        const struct city *target_city,
                        const struct unit *target_unit);

enum ane_kind action_not_enabled_reason(struct unit *punit,
                                        action_id act_id,
                                        const struct tile *target_tile,
                                        const struct city *target_city,
                                        const struct unit *target_unit);

bool unit_server_side_agent_set(struct player *pplayer,
                                struct unit *punit,
                                enum server_side_agent agent);

void create_trade_route(struct city *from, struct city *to,
                        struct goods_type *goods);

#endif /* FC__UNITHAND_H */
