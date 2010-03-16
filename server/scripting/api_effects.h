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

#ifndef FC__API_EFFECTS_H
#define FC__API_EFFECTS_H

#include "api_types.h"

int api_effects_world_bonus(const char *effect_type);
int api_effects_player_bonus(Player *pplayer, const char *effect_type);
int api_effects_city_bonus(City *pcity, const char *effect_type);

#endif
