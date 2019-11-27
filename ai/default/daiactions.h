/**********************************************************************
 Freeciv - Copyright (C) 2019 - The Freeciv Project contributors.
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC_DAI_ACTIONS_H
#define FC_DAI_ACTIONS_H

/* common */
#include "actions.h"
#include "fc_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

adv_want dai_action_value_unit_vs_city(struct action *paction,
                                       struct unit *actor_unit,
                                       struct city *target_city,
                                       int sub_tgt_id,
                                       int count_tech);

int dai_action_choose_sub_tgt_unit_vs_city(struct action *paction,
                                           struct unit *actor_unit,
                                           struct city *target_city);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC_DAI_ACTIONS_H */
