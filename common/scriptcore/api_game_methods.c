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
#include "game.h"
#include "government.h"
#include "improvement.h"
#include "map.h"
#include "movement.h"
#include "nation.h"
#include "tech.h"
#include "terrain.h"
#include "tile.h"
#include "unitlist.h"
#include "unittype.h"

/* common/scriptcore */
#include "luascript.h"

#include "api_game_methods.h"


/*****************************************************************************
  Return the current turn.
*****************************************************************************/
int api_methods_game_turn(lua_State *L)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);

  return game.info.turn;
}

/*****************************************************************************
  Return TRUE if pbuilding is a wonder.
*****************************************************************************/
bool api_methods_building_type_is_wonder(lua_State *L,
                                         Building_Type *pbuilding)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pbuilding, FALSE);

  return is_wonder(pbuilding);
}

/*****************************************************************************
  Return TRUE if pbuilding is a great wonder.
*****************************************************************************/
bool api_methods_building_type_is_great_wonder(lua_State *L,
                                               Building_Type *pbuilding)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pbuilding, FALSE);

  return is_great_wonder(pbuilding);
}

/*****************************************************************************
  Return TRUE if pbuilding is a small wonder.
*****************************************************************************/
bool api_methods_building_type_is_small_wonder(lua_State *L,
                                               Building_Type *pbuilding)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pbuilding, FALSE);

  return is_small_wonder(pbuilding);
}

/*****************************************************************************
  Return TRUE if pbuilding is a building.
*****************************************************************************/
bool api_methods_building_type_is_improvement(lua_State *L,
                                              Building_Type *pbuilding)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pbuilding, FALSE);

  return is_improvement(pbuilding);
}

/*****************************************************************************
  Return rule name for Building_Type
*****************************************************************************/
const char *api_methods_building_type_rule_name(lua_State *L,
                                                Building_Type *pbuilding)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, pbuilding, NULL);

  return improvement_rule_name(pbuilding);
}

/*****************************************************************************
  Return translated name for Building_Type
*****************************************************************************/
const char
  *api_methods_building_type_name_translation(lua_State *L,
                                              Building_Type *pbuilding)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, pbuilding, NULL);

  return improvement_name_translation(pbuilding);
}


/*****************************************************************************
  Return TRUE iff city has building
*****************************************************************************/
bool api_methods_city_has_building(lua_State *L, City *pcity,
                                   Building_Type *building)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, pcity, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, building, 3, Building_Type, FALSE);

  return city_has_building(pcity, building);
}

/*****************************************************************************
  Return the square raduis of the city map.
*****************************************************************************/
int api_methods_city_map_sq_radius(lua_State *L, City *pcity)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pcity, 0);

  return city_map_radius_sq_get(pcity);
}

/**************************************************************************
  Return the size of the city.
**************************************************************************/
int api_methods_city_size_get(lua_State *L, City *pcity)
{
  LUASCRIPT_CHECK_STATE(L, 1);
  LUASCRIPT_CHECK_SELF(L, pcity, 1);

  return city_size_get(pcity);
}

/**************************************************************************
  Return the tile of the city.
**************************************************************************/
Tile *api_methods_city_tile_get(lua_State *L, City *pcity)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, pcity, NULL);

  return pcity->tile;
}

/*****************************************************************************
  Return rule name for Government
*****************************************************************************/
const char *api_methods_government_rule_name(lua_State *L,
                                             Government *pgovernment)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, pgovernment, NULL);

  return government_rule_name(pgovernment);
}

/*****************************************************************************
  Return translated name for Government
*****************************************************************************/
const char *api_methods_government_name_translation(lua_State *L,
                                                    Government *pgovernment)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, pgovernment, NULL);

  return government_name_translation(pgovernment);
}


/*****************************************************************************
  Return rule name for Nation_Type
*****************************************************************************/
const char *api_methods_nation_type_rule_name(lua_State *L,
                                              Nation_Type *pnation)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, pnation, NULL);

  return nation_rule_name(pnation);
}

/*****************************************************************************
  Return translated adjective for Nation_Type
*****************************************************************************/
const char *api_methods_nation_type_name_translation(lua_State *L,
                                                     Nation_Type *pnation)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, pnation, NULL);

  return nation_adjective_translation(pnation);
}

/*****************************************************************************
  Return translated plural noun for Nation_Type
*****************************************************************************/
const char *api_methods_nation_type_plural_translation(lua_State *L,
                                                       Nation_Type *pnation)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, pnation, NULL);

  return nation_plural_translation(pnation);
}


/*****************************************************************************
  Return TRUE iff player has wonder
*****************************************************************************/
bool api_methods_player_has_wonder(lua_State *L, Player *pplayer,
                                   Building_Type *building)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pplayer, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, building, 3, Building_Type, FALSE);

  return wonder_is_built(pplayer, building);
}

/*****************************************************************************
  Return player number
*****************************************************************************/
int api_methods_player_number(lua_State *L, Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, pplayer, -1);

  return player_number(pplayer);
}

/*****************************************************************************
  Return the number of cities pplayer has.
*****************************************************************************/
int api_methods_player_num_cities(lua_State *L, Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);

  return city_list_size(pplayer->cities);
}

/*****************************************************************************
  Return the number of units pplayer has.
*****************************************************************************/
int api_methods_player_num_units(lua_State *L, Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);

  return unit_list_size(pplayer->units);
}

/*****************************************************************************
  Return gold for Player
*****************************************************************************/
int api_methods_player_gold(lua_State *L, Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);

  return pplayer->economic.gold;
}

/*****************************************************************************
  Return TRUE if Player knows advance ptech.
*****************************************************************************/
bool api_methods_player_knows_tech(lua_State *L, Player *pplayer,
                                   Tech_Type *ptech)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pplayer, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, ptech, 3, Tech_Type, FALSE);

  return player_invention_state(pplayer, advance_number(ptech)) == TECH_KNOWN;
}

/*****************************************************************************
  Return list head for unit list for Player
*****************************************************************************/
Unit_List_Link *api_methods_private_player_unit_list_head(lua_State *L,
                                                          Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, pplayer, NULL);
  return unit_list_head(pplayer->units);
}

/*****************************************************************************
  Return list head for city list for Player
*****************************************************************************/
City_List_Link *api_methods_private_player_city_list_head(lua_State *L,
                                                          Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, pplayer, NULL);

  return city_list_head(pplayer->cities);
}

/*****************************************************************************
  Return rule name for Tech_Type
*****************************************************************************/
const char *api_methods_tech_type_rule_name(lua_State *L, Tech_Type *ptech)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, ptech, NULL);

  return advance_rule_name(ptech);
}

/*****************************************************************************
  Return translated name for Tech_Type
*****************************************************************************/
const char *api_methods_tech_type_name_translation(lua_State *L,
                                                   Tech_Type *ptech)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, ptech, NULL);

  return advance_name_translation(ptech);
}

/*****************************************************************************
  Return rule name for Terrain
*****************************************************************************/
const char *api_methods_terrain_rule_name(lua_State *L, Terrain *pterrain)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, pterrain, NULL);

  return terrain_rule_name(pterrain);
}

/*****************************************************************************
  Return translated name for Terrain
*****************************************************************************/
const char *api_methods_terrain_name_translation(lua_State *L,
                                                 Terrain *pterrain)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, pterrain, NULL);

  return terrain_name_translation(pterrain);
}

/*****************************************************************************
  Return rule name for Disaster
*****************************************************************************/
const char *api_methods_disaster_rule_name(lua_State *L, Disaster *pdis)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, pdis, NULL);

  return disaster_rule_name(pdis);
}

/*****************************************************************************
  Return translated name for Disaster
*****************************************************************************/
const char *api_methods_disaster_name_translation(lua_State *L,
                                                  Disaster *pdis)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, pdis, NULL);

  return disaster_name_translation(pdis);
}

/*****************************************************************************
  Return the native x coordinate of the tile.
*****************************************************************************/
int api_methods_tile_nat_x(lua_State *L, Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, ptile, -1);

  return index_to_native_pos_x(tile_index(ptile));
}

/*****************************************************************************
  Return the native y coordinate of the tile.
*****************************************************************************/
int api_methods_tile_nat_y(lua_State *L, Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, ptile, -1);

  return index_to_native_pos_y(tile_index(ptile));
}

/*****************************************************************************
  Return the map x coordinate of the tile.
*****************************************************************************/
int api_methods_tile_map_x(lua_State *L, Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, ptile, -1);

  return index_to_map_pos_x(tile_index(ptile));
}

/*****************************************************************************
  Return the map y coordinate of the tile.
*****************************************************************************/
int api_methods_tile_map_y(lua_State *L, Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, ptile, -1);

  return index_to_map_pos_y(tile_index(ptile));
}

/*****************************************************************************
  Return City on ptile, else NULL
*****************************************************************************/
City *api_methods_tile_city(lua_State *L, Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, ptile, NULL);

  return tile_city(ptile);
}

/*****************************************************************************
  Return TRUE if there is a city inside the maximum city radius from ptile.
*****************************************************************************/
bool api_methods_tile_city_exists_within_max_city_map(lua_State *L,
                                                      Tile *ptile,
                                                      bool may_be_on_center)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, ptile, FALSE);

  return city_exists_within_max_city_map(ptile, may_be_on_center);
}

/*****************************************************************************
  Return TRUE if there is a base with rule name name on ptile.
  If no name is specified return true if there is a base on ptile.
*****************************************************************************/
bool api_methods_tile_has_base(lua_State *L, Tile *ptile, const char *name)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, ptile, FALSE);

  struct base_type *base;
  if (!name) {
    return tile_has_any_bases(ptile);
  } else {
    base = base_type_by_rule_name(name);
    return tile_has_base(ptile, base);
  }
}

/*****************************************************************************
  Return number of units on tile
*****************************************************************************/
int api_methods_tile_num_units(lua_State *L, Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, ptile, 0);

  return unit_list_size(ptile->units);
}

/*****************************************************************************
  Return list head for unit list for Tile
*****************************************************************************/
Unit_List_Link *api_methods_private_tile_unit_list_head(lua_State *L,
                                                        Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, ptile, NULL);

  return unit_list_head(ptile->units);
}

/*****************************************************************************
  Return nth tile iteration index (for internal use)
  Will return the next index, or an index < 0 when done
*****************************************************************************/
int api_methods_private_tile_next_outward_index(lua_State *L, Tile *pstart,
                                                int index, int max_dist)
{
  int dx, dy;
  int newx, newy;
  int startx, starty;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pstart, 0);

  if (index < 0) {
    return 0;
  }

  index_to_map_pos(&startx, &starty, tile_index(pstart));

  index++;
  while (index < map.num_iterate_outwards_indices) {
    if (map.iterate_outwards_indices[index].dist > max_dist) {
      return -1;
    }
    dx = map.iterate_outwards_indices[index].dx;
    dy = map.iterate_outwards_indices[index].dy;
    newx = dx + startx;
    newy = dy + starty;
    if (!normalize_map_pos(&newx, &newy)) {
      index++;
      continue;
    }
    return index;
  }
  return -1;
}

/*****************************************************************************
  Return tile for nth iteration index (for internal use)
*****************************************************************************/
Tile *api_methods_private_tile_for_outward_index(lua_State *L, Tile *pstart,
                                                 int index)
{
  int newx, newy;

  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, pstart, NULL);
  LUASCRIPT_CHECK_ARG(L, index >= 0
                         && index < map.num_iterate_outwards_indices, 3,
                      "index out of bounds", NULL);

  index_to_map_pos(&newx, &newy, tile_index(pstart));
  newx += map.iterate_outwards_indices[index].dx;
  newy += map.iterate_outwards_indices[index].dy;

  if (!normalize_map_pos(&newx, &newy)) {
    return NULL;
  }
  return map_pos_to_tile(newx, newy);
}

/*****************************************************************************
  Return squared distance between tiles 1 and 2
*****************************************************************************/
int api_methods_tile_sq_distance(lua_State *L, Tile *ptile1, Tile *ptile2)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, ptile1, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile2, 3, Tile, 0);

  return sq_map_distance(ptile1, ptile2);
}

/*****************************************************************************
  Can punit found a city on its tile?
*****************************************************************************/
bool api_methods_unit_city_can_be_built_here(lua_State *L, Unit *punit)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, punit, FALSE);

  return city_can_be_built_here(unit_tile(punit), punit);
}

/**************************************************************************
  Return the tile of the unit.
**************************************************************************/
Tile *api_methods_unit_tile_get(lua_State *L, Unit *punit)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, punit, NULL);

  return unit_tile(punit);
}

/*****************************************************************************
  Get unit orientation
*****************************************************************************/
Direction api_methods_unit_orientation_get(lua_State *L, Unit *punit)
{
  LUASCRIPT_CHECK_STATE(L, direction8_invalid());
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit, direction8_invalid());

  return punit->facing;
}

/*****************************************************************************
  Return TRUE if punit_type has flag.
*****************************************************************************/
bool api_methods_unit_type_has_flag(lua_State *L, Unit_Type *punit_type,
                                    const char *flag)
{
  enum unit_type_flag_id id;

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, punit_type, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, flag, 3, string, FALSE);

  id = unit_type_flag_id_by_name(flag, fc_strcasecmp);
  if (unit_type_flag_id_is_valid(id)) {
    return utype_has_flag(punit_type, id);
  } else {
    luascript_error(L, "Unit type flag \"%s\" does not exist", flag);
    return FALSE;
  }
}

/*****************************************************************************
  Return TRUE if punit_type has role.
*****************************************************************************/
bool api_methods_unit_type_has_role(lua_State *L, Unit_Type *punit_type,
                                    const char *role)
{
  enum unit_role_id id;

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, punit_type, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, role, 3, string, FALSE);

  id = unit_role_by_rule_name(role);
  if (id != L_LAST) {
    return utype_has_role(punit_type, id);
  } else {
    luascript_error(L, "Unit role \"%s\" does not exist", role);
    return FALSE;
  }
}

/*****************************************************************************
  Return TRUE iff the unit type can exist on the tile.
*****************************************************************************/
bool api_methods_unit_type_can_exist_at_tile(lua_State *L,
                                             Unit_Type *punit_type,
                                             Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, punit_type, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 3, Tile, FALSE);

  return can_exist_at_tile(punit_type, ptile);
}

/*****************************************************************************
  Return rule name for Unit_Type
*****************************************************************************/
const char *api_methods_unit_type_rule_name(lua_State *L,
                                            Unit_Type *punit_type)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, punit_type, NULL);

  return utype_rule_name(punit_type);
}

/*****************************************************************************
  Return translated name for Unit_Type
*****************************************************************************/
const char *api_methods_unit_type_name_translation(lua_State *L,
                                                   Unit_Type *punit_type)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_SELF(L, punit_type, NULL);

  return utype_name_translation(punit_type);
}


/*****************************************************************************
  Return Unit for list link
*****************************************************************************/
Unit *api_methods_unit_list_link_data(lua_State *L,
                                      Unit_List_Link *link)
{
  LUASCRIPT_CHECK_STATE(L, NULL);

  return unit_list_link_data(link);
}

/*****************************************************************************
  Return next list link or NULL when link is the last link
*****************************************************************************/
Unit_List_Link *api_methods_unit_list_next_link(lua_State *L,
                                                Unit_List_Link *link)
{
  LUASCRIPT_CHECK_STATE(L, NULL);

  return unit_list_link_next(link);
}

/*****************************************************************************
  Return City for list link
*****************************************************************************/
City *api_methods_city_list_link_data(lua_State *L,
                                      City_List_Link *link)
{
  LUASCRIPT_CHECK_STATE(L, NULL);

  return city_list_link_data(link);
}

/*****************************************************************************
  Return next list link or NULL when link is the last link
*****************************************************************************/
City_List_Link *api_methods_city_list_next_link(lua_State *L,
                                                City_List_Link *link)
{
  LUASCRIPT_CHECK_STATE(L, NULL);

  return city_list_link_next(link);
}
