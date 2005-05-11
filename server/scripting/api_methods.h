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

#ifndef FC__API_METHODS_H
#define FC__API_METHODS_H

#include "api_types.h"

int api_methods_player_num_cities(Player *pplayer);
int api_methods_player_num_units(Player *pplayer);

bool api_methods_unit_type_has_flag(Unit_Type *punit_type, const char *flag);
bool api_methods_unit_type_has_role(Unit_Type *punit_type, const char *role);

bool api_methods_building_type_is_wonder(Building_Type *pbuilding);
bool api_methods_building_type_is_great_wonder(Building_Type *pbuilding);
bool api_methods_building_type_is_small_wonder(Building_Type *pbuilding);
bool api_methods_building_type_is_improvement(Building_Type *pbuilding);

#endif

