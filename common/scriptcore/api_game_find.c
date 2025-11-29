/*****************************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* common */
#include "counters.h"
#include "idex.h"
#include "map.h"
#include "movement.h"

/* common/scriptcore */
#include "luascript.h"

#include "api_game_find.h"

/**********************************************************************//**
  Return a player with the given name
**************************************************************************/
Player *api_find_player_by_name(lua_State *L, const char *name)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return player_by_name(name);
}

/**********************************************************************//**
  Return a player with the given player_id.
**************************************************************************/
Player *api_find_player(lua_State *L, int player_id)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return player_by_number(player_id);
}

/**********************************************************************//**
  Return a player city with the given city_id.
**************************************************************************/
City *api_find_city(lua_State *L, Player *pplayer, int city_id)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  if (pplayer) {
    return player_city_by_number(pplayer, city_id);
  } else {
    return idex_lookup_city(&wld, city_id);
  }
}

/**********************************************************************//**
  Return a player unit with the given unit_id.
**************************************************************************/
Unit *api_find_unit(lua_State *L, Player *pplayer, int unit_id)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  if (pplayer) {
    return player_unit_by_number(pplayer, unit_id);
  } else {
    return idex_lookup_unit(&wld, unit_id);
  }
}

/**********************************************************************//**
  Return a unit that can transport ptype at a given ptile.
**************************************************************************/
Unit *api_find_transport_unit(lua_State *L, Player *pplayer,
                              Unit_Type *ptype, Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 2, Player, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, ptype, 3, Unit_Type, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 4, Tile, nullptr);

  {
    struct unit *ptransport;
    struct unit *pvirt = unit_virtual_create(pplayer, nullptr, ptype, 0);
    unit_tile_set(pvirt, ptile);
    pvirt->homecity = 0;
    ptransport = transporter_for_unit(pvirt);
    unit_virtual_destroy(pvirt);
    return ptransport;
  }
}

/**********************************************************************//**
  Return a unit type for given role or flag.
  (Prior to 2.6.0, this worked only for roles.)
**************************************************************************/
Unit_Type *api_find_role_unit_type(lua_State *L, const char *role_name,
                                   Player *pplayer)
{
  int role_or_flag;

  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, role_name, 2, string, nullptr);

  role_or_flag = unit_role_id_by_name(role_name, fc_strcasecmp);

  if (!unit_role_id_is_valid(role_or_flag)) {
    role_or_flag = unit_type_flag_id_by_name(role_name, fc_strcasecmp);
    if (!unit_type_flag_id_is_valid(role_or_flag)) {
      return nullptr;
    }
  }

  if (pplayer) {
    return best_role_unit_for_player(pplayer, role_or_flag);
  } else if (num_role_units(role_or_flag) > 0) {
    return get_role_unit(role_or_flag, 0);
  } else {
    return nullptr;
  }
}

/**********************************************************************//**
  Return the tile at the given native coordinates.
**************************************************************************/
Tile *api_find_tile(lua_State *L, int nat_x, int nat_y)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return native_pos_to_tile(&(wld.map), nat_x, nat_y);
}

/**********************************************************************//**
  Return the tile at the given index.
**************************************************************************/
Tile *api_find_tile_by_index(lua_State *L, int tindex)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return index_to_tile(&(wld.map), tindex);
}

/**********************************************************************//**
  Return the government with the given Government_type_id index.
**************************************************************************/
Government *api_find_government(lua_State *L, int government_id)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return government_by_number(government_id);
}

/**********************************************************************//**
  Return the governmet with the given name_orig.
**************************************************************************/
Government *api_find_government_by_name(lua_State *L,
                                        const char *name_orig)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, name_orig, 2, string, nullptr);

  return government_by_rule_name(name_orig);
}

/**********************************************************************//**
  Return the nation type with the given nation_type_id index.
**************************************************************************/
Nation_Type *api_find_nation_type(lua_State *L, int nation_type_id)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return nation_by_number(nation_type_id);
}

/**********************************************************************//**
  Return the nation type with the given name_orig.
**************************************************************************/
Nation_Type *api_find_nation_type_by_name(lua_State *L,
                                          const char *name_orig)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, name_orig, 2, string, nullptr);

  return nation_by_rule_name(name_orig);
}

/**********************************************************************//**
  Return the improvement type with the given impr_type_id index.
**************************************************************************/
Building_Type *api_find_building_type(lua_State *L, int building_type_id)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return improvement_by_number(building_type_id);
}

/**********************************************************************//**
  Return the improvement type with the given name_orig.
**************************************************************************/
Building_Type *api_find_building_type_by_name(lua_State *L,
                                              const char *name_orig)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, name_orig, 2, string, nullptr);

  return improvement_by_rule_name(name_orig);
}

/**********************************************************************//**
  Return the unit type with the given unit_type_id index.
**************************************************************************/
Unit_Type *api_find_unit_type(lua_State *L, int unit_type_id)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return utype_by_number(unit_type_id);
}

/**********************************************************************//**
  Return the unit type with the given name_orig.
**************************************************************************/
Unit_Type *api_find_unit_type_by_name(lua_State *L, const char *name_orig)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, name_orig, 2, string, nullptr);

  return unit_type_by_rule_name(name_orig);
}

/**********************************************************************//**
  Return the tech type with the given tech_type_id index.
**************************************************************************/
Tech_Type *api_find_tech_type(lua_State *L, int tech_type_id)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return advance_by_number(tech_type_id);
}

/**********************************************************************//**
  Return the tech type with the given name_orig.
**************************************************************************/
Tech_Type *api_find_tech_type_by_name(lua_State *L, const char *name_orig)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, name_orig, 2, string, nullptr);

  return advance_by_rule_name(name_orig);
}

/**********************************************************************//**
  Return the terrain with the given terrain_id index.
**************************************************************************/
Terrain *api_find_terrain(lua_State *L, int terrain_id)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return terrain_by_number(terrain_id);
}

/**********************************************************************//**
  Return the terrain with the given name_orig.
**************************************************************************/
Terrain *api_find_terrain_by_name(lua_State *L, const char *name_orig)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, name_orig, 2, string, nullptr);

  return terrain_by_rule_name(name_orig);
}

/**********************************************************************//**
  Return the achievement with the given achievement_id index.
**************************************************************************/
Achievement *api_find_achievement(lua_State *L, int achievement_id)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return achievement_by_number(achievement_id);
}

/**********************************************************************//**
  Return the achievement with the given name_orig.
**************************************************************************/
Achievement *api_find_achievement_by_name(lua_State *L, const char *name_orig)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, name_orig, 2, string, nullptr);

  return achievement_by_rule_name(name_orig);
}

/**********************************************************************//**
  Return the specialist with the given name_orig.
**************************************************************************/
Specialist *api_find_specialist_by_name(lua_State *L, const char *name_orig)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, name_orig, 2, string, nullptr);

  return specialist_by_rule_name(name_orig);
}

/**********************************************************************//**
  Return the specialist with the given spec_id index.
**************************************************************************/
Specialist *api_find_specialist(lua_State *L, int spec_id)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return specialist_by_number((Specialist_type_id) spec_id);
}

/**********************************************************************//**
  Return the disaster with the given disaster_id index.
**************************************************************************/
Disaster *api_find_disaster(lua_State *L, int disaster_id)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return disaster_by_number(disaster_id);
}

/**********************************************************************//**
  Return the disaster with the given name_orig.
**************************************************************************/
Disaster *api_find_disaster_by_name(lua_State *L, const char *name_orig)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, name_orig, 2, string, nullptr);

  return disaster_by_rule_name(name_orig);
}

/**********************************************************************//**
  Return the direction with the given id
**************************************************************************/
const Direction *api_find_direction(lua_State *L, int id)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return luascript_dir((enum direction8) id);
}

/**********************************************************************//**
  Returns the action with the specified id.
**************************************************************************/
Action *api_find_action_type_by_id(lua_State *L, int id)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return action_by_number(id);
}

/**********************************************************************//**
  Returns the action with the specified name.
**************************************************************************/
Action *api_find_action_type_by_name(lua_State *L, const char *name)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, name, 2, string, nullptr);

  return action_by_rule_name(name);
}

/**********************************************************************//**
  Returns the counter specified by id.
**************************************************************************/
Counter *api_find_counter(lua_State *L, int counter_id)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return counter_by_id(counter_id);
}

/**********************************************************************//**
  Returns the counter specified by name.
**************************************************************************/
Counter *api_find_counter_by_name(lua_State *L, const char *name)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, name, 2, string, nullptr);

  return counter_by_rule_name(name);
}

/**********************************************************************//**
  Return a dummy pointer.
**************************************************************************/
Nonexistent *api_find_nonexistent(lua_State *L)
{
  static char *p = "";

  LUASCRIPT_CHECK_STATE(L, nullptr);

  return p;
}
