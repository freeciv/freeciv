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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "unitlist.h"

#include "api_methods.h"
#include "script.h"

/**************************************************************************
  Return the number of cities pplayer has.
**************************************************************************/
int api_methods_player_num_cities(Player *pplayer)
{
  return city_list_size(pplayer->cities);
}

/**************************************************************************
  Return the number of units pplayer has.
**************************************************************************/
int api_methods_player_num_units(Player *pplayer)
{
  return unit_list_size(pplayer->units);
}

/**************************************************************************
  Return TRUE if punit_type has flag.
**************************************************************************/
bool api_methods_unit_type_has_flag(Unit_Type *punit_type, const char *flag)
{
  enum unit_flag_id id = find_unit_flag_by_rule_name(flag);

  if (id != F_LAST) {
    return utype_has_flag(punit_type, id);
  } else {
    script_error("Unit flag \"%s\" does not exist", flag);
    return FALSE;
  }
}

/**************************************************************************
  Return TRUE if punit_type has role.
**************************************************************************/
bool api_methods_unit_type_has_role(Unit_Type *punit_type, const char *role)
{
  enum unit_role_id id = find_unit_role_by_rule_name(role);

  if (id != L_LAST) {
    return utype_has_role(punit_type, id);
  } else {
    script_error("Unit role \"%s\" does not exist", role);
    return FALSE;
  }
}

/**************************************************************************
  Return TRUE if pbuilding is a wonder.
**************************************************************************/
bool api_methods_building_type_is_wonder(Building_Type *pbuilding)
{
  return is_wonder(pbuilding->index);
}

/**************************************************************************
  Return TRUE if pbuilding is a great wonder.
**************************************************************************/
bool api_methods_building_type_is_great_wonder(Building_Type *pbuilding)
{
  return is_great_wonder(pbuilding->index);
}

/**************************************************************************
  Return TRUE if pbuilding is a small wonder.
**************************************************************************/
bool api_methods_building_type_is_small_wonder(Building_Type *pbuilding)
{
  return is_small_wonder(pbuilding->index);
}

/**************************************************************************
  Return TRUE if pbuilding is a building.
**************************************************************************/
bool api_methods_building_type_is_improvement(Building_Type *pbuilding)
{
  return is_improvement(pbuilding->index);
}


