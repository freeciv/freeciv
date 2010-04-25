/**********************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__API_ACTIONS_H
#define FC__API_ACTIONS_H

#include "api_types.h"

bool api_actions_unleash_barbarians(Tile *ptile);
void api_actions_place_partisans(Tile *ptile, Player *pplayer,
                                 int count, int sq_radius);
Unit *api_actions_create_unit(Player *pplayer, Tile *ptile, Unit_Type *ptype,
                              int veteran_level, City *homecity,
                              int moves_left);
Unit *api_actions_create_unit_full(Player *pplayer, Tile *ptile,
                                   Unit_Type *ptype,
                                   int veteran_level, City *homecity,
                                   int moves_left, int hp_left,
                                   Unit *ptransport);
void api_actions_create_city(Player *pplayer, Tile *ptile, const char *name);
Player *api_actions_create_player(const char *username,
                                  Nation_Type *pnation);
void api_actions_change_gold(Player *pplayer, int amount);
Tech_Type *api_actions_give_technology(Player *pplayer, Tech_Type *ptech,
                                       const char *reason);

void api_actions_create_base(Tile *ptile, const char *name,
                             struct player *pplayer);

#endif
