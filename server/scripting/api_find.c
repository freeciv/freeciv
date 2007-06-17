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

#include "idex.h"

#include "api_find.h"


/**************************************************************************
  Return a player with the given player_id.
**************************************************************************/
Player *api_find_player(int player_id)
{
  return get_player(player_id);
}

/**************************************************************************
  Return a player city with the given city_id.
**************************************************************************/
City *api_find_city(Player *pplayer, int city_id)
{
  if (pplayer) {
    return player_find_city_by_id(pplayer, city_id);
  } else {
    return idex_lookup_city(city_id);
  }
}

/**************************************************************************
  Return a player unit with the given unit_id.
**************************************************************************/
Unit *api_find_unit(Player *pplayer, int unit_id)
{
  if (pplayer) {
    return player_find_unit_by_id(pplayer, unit_id);
  } else {
    return idex_lookup_unit(unit_id);
  }
}

/**************************************************************************
  Return the tile at the given native coordinates.
**************************************************************************/
Tile *api_find_tile(int nat_x, int nat_y)
{
  return native_pos_to_tile(nat_x, nat_y);
}

/**************************************************************************
  Return the government with the given government_id index.
**************************************************************************/
Government *api_find_government(int government_id)
{
  return get_government(government_id);
}

/**************************************************************************
  Return the governmet with the given name_orig.
**************************************************************************/
Government *api_find_government_by_name(const char *name_orig)
{
  return find_government_by_name_orig(name_orig);
}

/**************************************************************************
  Return the nation type with the given nation_type_id index.
**************************************************************************/
Nation_Type *api_find_nation_type(int nation_type_id)
{
  return get_nation_by_idx(nation_type_id);
}

/**************************************************************************
  Return the nation type with the given name_orig.
**************************************************************************/
Nation_Type *api_find_nation_type_by_name(const char *name_orig)
{
  return find_nation_by_name_orig(name_orig);
}

/**************************************************************************
  Return the improvement type with the given impr_type_id index.
**************************************************************************/
Building_Type *api_find_building_type(int building_type_id)
{
  return get_improvement_type(building_type_id);
}

/**************************************************************************
  Return the improvement type with the given name_orig.
**************************************************************************/
Building_Type *api_find_building_type_by_name(const char *name_orig)
{
  Impr_type_id id = find_improvement_by_name_orig(name_orig);
  return api_find_building_type(id);
}

/**************************************************************************
  Return the unit type with the given unit_type_id index.
**************************************************************************/
Unit_Type *api_find_unit_type(int unit_type_id)
{
  return get_unit_type(unit_type_id);
}

/**************************************************************************
  Return the unit type with the given name_orig.
**************************************************************************/
Unit_Type *api_find_unit_type_by_name(const char *name_orig)
{
  return find_unit_type_by_name_orig(name_orig);
}

/**************************************************************************
  Return the tech type with the given tech_type_id index.
**************************************************************************/
Tech_Type *api_find_tech_type(int tech_type_id)
{
  return &advances[tech_type_id];
}

/**************************************************************************
  Return the tech type with the given name_orig.
**************************************************************************/
Tech_Type *api_find_tech_type_by_name(const char *name_orig)
{
  Tech_type_id id = find_tech_by_name_orig(name_orig);
  return api_find_tech_type(id);
}

/**************************************************************************
  Return the terrain with the given terrain_id index.
**************************************************************************/
Terrain *api_find_terrain(int terrain_id)
{
  return get_terrain_by_number(terrain_id);
}

/**************************************************************************
  Return the terrain with the given name_orig.
**************************************************************************/
Terrain *api_find_terrain_by_name(const char *name_orig)
{
  struct terrain *pterrain = get_terrain_by_rule_name(name_orig);

  return pterrain;
}

