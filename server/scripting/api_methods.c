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

/* common */
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

/* server */
#include "score.h"
#include "plrhand.h"

/* server/scripting */
#include "script.h"

#include "api_methods.h"


/**************************************************************************
  Return TRUE if pbuilding is a wonder.
**************************************************************************/
bool api_methods_building_type_is_wonder(Building_Type *pbuilding)
{
  SCRIPT_CHECK_SELF(pbuilding, FALSE);
  return is_wonder(pbuilding);
}

/**************************************************************************
  Return TRUE if pbuilding is a great wonder.
**************************************************************************/
bool api_methods_building_type_is_great_wonder(Building_Type *pbuilding)
{
  SCRIPT_CHECK_SELF(pbuilding, FALSE);
  return is_great_wonder(pbuilding);
}

/**************************************************************************
  Return TRUE if pbuilding is a small wonder.
**************************************************************************/
bool api_methods_building_type_is_small_wonder(Building_Type *pbuilding)
{
  SCRIPT_CHECK_SELF(pbuilding, FALSE);
  return is_small_wonder(pbuilding);
}

/**************************************************************************
  Return TRUE if pbuilding is a building.
**************************************************************************/
bool api_methods_building_type_is_improvement(Building_Type *pbuilding)
{
  SCRIPT_CHECK_SELF(pbuilding, FALSE);
  return is_improvement(pbuilding);
}

/**************************************************************************
  Return rule name for Building_Type
**************************************************************************/
const char *api_methods_building_type_rule_name(Building_Type *pbuilding)
{
  SCRIPT_CHECK_SELF(pbuilding, NULL);
  return improvement_rule_name(pbuilding);
}

/**************************************************************************
  Return translated name for Building_Type
**************************************************************************/
const char *api_methods_building_type_name_translation(Building_Type 
                                                       *pbuilding)
{
  SCRIPT_CHECK_SELF(pbuilding, NULL);
  return improvement_name_translation(pbuilding);
}


/**************************************************************************
  Return TRUE iff city has building
**************************************************************************/
bool api_methods_city_has_building(City *pcity, Building_Type *building)
{
  SCRIPT_CHECK_SELF(pcity, FALSE);
  SCRIPT_CHECK_ARG_NIL(building, 2, Building_Type, FALSE);
  return city_has_building(pcity, building);
}

/**************************************************************************
  Return the square raduis of the city map.
**************************************************************************/
int api_methods_city_map_sq_radius(City *pcity)
{
  SCRIPT_CHECK_SELF(pcity, 0);
  return city_map_radius_sq_get(pcity);
}


/**************************************************************************
  Return rule name for Government
**************************************************************************/
const char *api_methods_government_rule_name(Government *pgovernment)
{
  SCRIPT_CHECK_SELF(pgovernment, NULL);
  return government_rule_name(pgovernment);
}

/**************************************************************************
  Return translated name for Government
**************************************************************************/
const char *api_methods_government_name_translation(Government *pgovernment)
{
  SCRIPT_CHECK_SELF(pgovernment, NULL);
  return government_name_translation(pgovernment);
}


/**************************************************************************
  Return rule name for Nation_Type
**************************************************************************/
const char *api_methods_nation_type_rule_name(Nation_Type *pnation)
{
  SCRIPT_CHECK_SELF(pnation, NULL);
  return nation_rule_name(pnation);
}

/**************************************************************************
  Return translated adjective for Nation_Type
**************************************************************************/
const char *api_methods_nation_type_name_translation(Nation_Type *pnation)
{
  SCRIPT_CHECK_SELF(pnation, NULL);
  return nation_adjective_translation(pnation);
}

/**************************************************************************
  Return translated plural noun for Nation_Type
**************************************************************************/
const char *api_methods_nation_type_plural_translation(Nation_Type *pnation)
{
  SCRIPT_CHECK_SELF(pnation, NULL);
  return nation_plural_translation(pnation);
}


/**************************************************************************
  Return TRUE iff player has wonder
**************************************************************************/
bool api_methods_player_has_wonder(Player *pplayer, Building_Type *building)
{
  SCRIPT_CHECK_SELF(pplayer, FALSE);
  SCRIPT_CHECK_ARG_NIL(building, 2, Building_Type, FALSE);
  return wonder_is_built(pplayer, building);
}

/**************************************************************************
  Return player number
**************************************************************************/
int api_methods_player_number(Player *pplayer)
{
  SCRIPT_CHECK_SELF(pplayer, -1);
  return player_number(pplayer);
}

/**************************************************************************
  Return the number of cities pplayer has.
**************************************************************************/
int api_methods_player_num_cities(Player *pplayer)
{
  SCRIPT_CHECK_SELF(pplayer, 0);
  return city_list_size(pplayer->cities);
}

/**************************************************************************
  Return the number of units pplayer has.
**************************************************************************/
int api_methods_player_num_units(Player *pplayer)
{
  SCRIPT_CHECK_SELF(pplayer, 0);
  return unit_list_size(pplayer->units);
}

/**************************************************************************
  Make player winner of the scenario
**************************************************************************/
void api_methods_player_victory(Player *pplayer)
{
  SCRIPT_CHECK_SELF(pplayer);
  player_status_add(pplayer, PSTATUS_WINNER);
}

/**************************************************************************
  Return the civilization score (total) for player
**************************************************************************/
int api_methods_player_civilization_score(Player *pplayer)
{
  SCRIPT_CHECK_SELF(pplayer, 0);
  return get_civ_score(pplayer);
}

/**************************************************************************
  Return gold for Player
**************************************************************************/
int api_methods_player_gold(Player *pplayer)
{
  SCRIPT_CHECK_SELF(pplayer, 0);
  return pplayer->economic.gold;
}

/**************************************************************************
  Return TRUE if Player knows advance ptech.
**************************************************************************/
bool api_methods_player_knows_tech(Player *pplayer, Tech_Type *ptech)
{
  SCRIPT_CHECK_SELF(pplayer, FALSE);
  SCRIPT_CHECK_ARG_NIL(ptech, 2, Tech_Type, FALSE);

  return player_invention_state(pplayer, advance_number(ptech)) == TECH_KNOWN;
}

/**************************************************************************
  Return list head for unit list for Player
**************************************************************************/
Unit_List_Link *api_methods_private_player_unit_list_head(Player *pplayer)
{
  SCRIPT_CHECK_SELF(pplayer, NULL);
  return unit_list_head(pplayer->units);
}

/**************************************************************************
  Return list head for city list for Player
**************************************************************************/
City_List_Link *api_methods_private_player_city_list_head(Player *pplayer)
{
  SCRIPT_CHECK_SELF(pplayer, NULL);
  return city_list_head(pplayer->cities);
}

/**************************************************************************
  Return rule name for Tech_Type
**************************************************************************/
const char *api_methods_tech_type_rule_name(Tech_Type *ptech)
{
  SCRIPT_CHECK_SELF(ptech, NULL);
  return advance_rule_name(ptech);
}

/**************************************************************************
  Return translated name for Tech_Type
**************************************************************************/
const char *api_methods_tech_type_name_translation(Tech_Type *ptech)
{
  SCRIPT_CHECK_SELF(ptech, NULL);
  return advance_name_translation(ptech);
}


/**************************************************************************
  Return rule name for Terrain
**************************************************************************/
const char *api_methods_terrain_rule_name(Terrain *pterrain)
{
  SCRIPT_CHECK_SELF(pterrain, NULL);
  return terrain_rule_name(pterrain);
}

/**************************************************************************
  Return translated name for Terrain
**************************************************************************/
const char *api_methods_terrain_name_translation(Terrain *pterrain)
{
  SCRIPT_CHECK_SELF(pterrain, NULL);
  return terrain_name_translation(pterrain);
}


/**************************************************************************
  Return City on ptile, else NULL
**************************************************************************/
City *api_methods_tile_city(Tile *ptile)
{
  SCRIPT_CHECK_SELF(ptile, NULL);
  return tile_city(ptile);
}

/**************************************************************************
  Return TRUE if there is a city inside the maximum city radius from ptile.
**************************************************************************/
bool api_methods_tile_city_exists_within_max_city_map(Tile *ptile,
                                                      bool may_be_on_center)
{
  SCRIPT_CHECK_SELF(ptile, FALSE);
  return city_exists_within_max_city_map(ptile, may_be_on_center);
}

/**************************************************************************
  Return number of units on tile
**************************************************************************/
int api_methods_tile_num_units(Tile *ptile)
{
  SCRIPT_CHECK_SELF(ptile, 0);
  return unit_list_size(ptile->units);
}

/**************************************************************************
  Return list head for unit list for Tile
**************************************************************************/
Unit_List_Link *api_methods_private_tile_unit_list_head(Tile *ptile)
{
  SCRIPT_CHECK_SELF(ptile, NULL);
  return unit_list_head(ptile->units);
}

/**************************************************************************
  Return nth tile iteration index (for internal use)
  Will return the next index, or an index < 0 when done
**************************************************************************/
int api_methods_private_tile_next_outward_index(Tile *pstart,
                                                int index,
                                                int max_dist)
{
  int dx, dy;
  int newx, newy;
  SCRIPT_CHECK_SELF(pstart, 0);

  if (index < 0) {
    return 0;
  }

  index++;
  while (index < map.num_iterate_outwards_indices) {
    if (map.iterate_outwards_indices[index].dist > max_dist) {
      return -1;
    }
    dx = map.iterate_outwards_indices[index].dx;
    dy = map.iterate_outwards_indices[index].dy;
    newx = dx + pstart->x;
    newy = dy + pstart->y;
    if (!normalize_map_pos(&newx, &newy)) {
      index++;
      continue;
    }
    return index;
  }
  return -1;
}

/**************************************************************************
  Return tile for nth iteration index (for internal use)
**************************************************************************/
Tile *api_methods_private_tile_for_outward_index(Tile *pstart, int index)
{
  int dx, dy;
  int newx, newy;
  SCRIPT_CHECK_SELF(pstart, NULL);
  SCRIPT_CHECK_ARG(index >= 0 && index < map.num_iterate_outwards_indices,
                   2, "index out of bounds", NULL);

  dx = map.iterate_outwards_indices[index].dx;
  dy = map.iterate_outwards_indices[index].dy;
  newx = dx + pstart->x;
  newy = dy + pstart->y;
  if (!normalize_map_pos(&newx, &newy)) {
    return NULL;
  }
  return map_pos_to_tile(newx, newy);
}

/**************************************************************************
  Return squared distance between tiles 1 and 2
**************************************************************************/
int api_methods_tile_sq_distance(Tile *ptile1, Tile *ptile2)
{
  SCRIPT_CHECK_SELF(ptile1, 0);
  SCRIPT_CHECK_ARG_NIL(ptile2, 2, Tile, 0);
  return sq_map_distance(ptile1, ptile2);
}


/**************************************************************************
  Can punit found a city on its tile?
**************************************************************************/
bool api_methods_unit_city_can_be_built_here(Unit *punit)
{
  SCRIPT_CHECK_SELF(punit, FALSE);
  return city_can_be_built_here(punit->tile, punit);
}


/**************************************************************************
  Return TRUE if punit_type has flag.
**************************************************************************/
bool api_methods_unit_type_has_flag(Unit_Type *punit_type, const char *flag)
{
  enum unit_flag_id id;
  SCRIPT_CHECK_SELF(punit_type, FALSE);
  SCRIPT_CHECK_ARG_NIL(flag, 2, string, FALSE);

  id = unit_flag_by_rule_name(flag);
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
  enum unit_role_id id;
  SCRIPT_CHECK_SELF(punit_type, FALSE);
  SCRIPT_CHECK_ARG_NIL(role, 2, string, FALSE);

  id = unit_role_by_rule_name(role);
  if (id != L_LAST) {
    return utype_has_role(punit_type, id);
  } else {
    script_error("Unit role \"%s\" does not exist", role);
    return FALSE;
  }
}

bool api_methods_unit_type_can_exist_at_tile(Unit_Type *punit_type,
                                             Tile *ptile)
{
  SCRIPT_CHECK_SELF(punit_type, FALSE);
  SCRIPT_CHECK_ARG_NIL(ptile, 2, Tile, FALSE);

  return can_exist_at_tile(punit_type, ptile);
}

/**************************************************************************
  Return rule name for Unit_Type
**************************************************************************/
const char *api_methods_unit_type_rule_name(Unit_Type *punit_type)
{
  SCRIPT_CHECK_SELF(punit_type, NULL);
  return utype_rule_name(punit_type);
}

/**************************************************************************
  Return translated name for Unit_Type
**************************************************************************/
const char *api_methods_unit_type_name_translation(Unit_Type *punit_type)
{
  SCRIPT_CHECK_SELF(punit_type, NULL);
  return utype_name_translation(punit_type);
}


/**************************************************************************
  Return Unit for list link
**************************************************************************/
Unit *api_methods_unit_list_link_data(Unit_List_Link *link)
{
  return unit_list_link_data(link);
}

/**************************************************************************
  Return next list link or NULL when link is the last link
**************************************************************************/
Unit_List_Link *api_methods_unit_list_next_link(Unit_List_Link *link)
{
  return unit_list_link_next(link);
}

/**************************************************************************
  Return City for list link
**************************************************************************/
City *api_methods_city_list_link_data(City_List_Link *link)
{
  return city_list_link_data(link);
}

/**************************************************************************
  Return next list link or NULL when link is the last link
**************************************************************************/
City_List_Link *api_methods_city_list_next_link(City_List_Link *link)
{
  return city_list_link_next(link);
}
