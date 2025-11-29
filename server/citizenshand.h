/*****************************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*****************************************************************************/
#ifndef FC__CITIZENSHAND_H
#define FC__CITIZENSHAND_H

struct city;

struct citizens_reduction {
  struct player_slot *pslot;
  citizens change;
};

void citizens_update(struct city *pcity, struct player *plr);
void citizens_convert(struct city *pcity);
void citizens_convert_conquest(struct city *pcity);
struct player
*citizens_unit_nationality(const struct city *pcity,
                           unsigned pop_cost,
                           struct citizens_reduction *pchange)
  fc__attribute((nonnull(1)));
void citizens_reduction_apply(struct city *pcity,
                              const struct citizens_reduction *pchange);

void citizens_print(const struct city *pcity);

#endif /* FC__CITIZENSHAND_H */
