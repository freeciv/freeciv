/***********************************************************************
 Freeciv - Copyright (C) 2020 - The Freeciv Project contributors.
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC_AI_ACTIONS_H
#define FC_AI_ACTIONS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct unit_type;

bool aia_utype_is_considered_spy_vs_city(const struct unit_type *putype);
bool aia_utype_is_considered_spy(const struct unit_type *putype);
bool aia_utype_is_considered_worker(const struct unit_type *putype);
bool aia_utype_is_considered_caravan_trade(const struct unit_type *putype);
bool aia_utype_is_considered_caravan(const struct unit_type *putype);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC_AI_ACTIONS_H */
