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

/* Building Type */
bool api_methods_building_type_is_wonder(Building_Type *pbuilding);
bool api_methods_building_type_is_great_wonder(Building_Type *pbuilding);
bool api_methods_building_type_is_small_wonder(Building_Type *pbuilding);
bool api_methods_building_type_is_improvement(Building_Type *pbuilding);
const char *api_methods_building_type_rule_name(Building_Type *pbuilding);
const char *api_methods_building_type_name_translation(Building_Type 
                                                       *pbuilding);

/* City */
bool api_methods_city_has_building(City *pcity, Building_Type *building);
int api_methods_city_map_sq_radius(City *pcity);

/* Government */
const char *api_methods_government_rule_name(Government *pgovernment);
const char *api_methods_government_name_translation(Government *pgovernment);

/* Nation */
const char *api_methods_nation_type_rule_name(Nation_Type *pnation);
const char *api_methods_nation_type_name_translation(Nation_Type *pnation);
const char *api_methods_nation_type_plural_translation(Nation_Type
                                                       *pnation);

/* Player */
bool api_methods_player_has_wonder(Player *pplayer, Building_Type *building);
int api_methods_player_number(Player *pplayer);
int api_methods_player_num_cities(Player *pplayer);
int api_methods_player_num_units(Player *pplayer);
void api_methods_player_victory(Player *pplayer);
int api_methods_player_civilization_score(Player *pplayer);
int api_methods_player_gold(Player *pplayer);
bool api_methods_player_knows_tech(Player *pplayer, Tech_Type *ptech);
Unit_List_Link *api_methods_private_player_unit_list_head(Player *pplayer);
City_List_Link *api_methods_private_player_city_list_head(Player *pplayer);

/* Tech Type */
const char *api_methods_tech_type_rule_name(Tech_Type *ptech);
const char *api_methods_tech_type_name_translation(Tech_Type *ptech);

/* Terrain */
const char *api_methods_terrain_rule_name(Terrain *pterrain);
const char *api_methods_terrain_name_translation(Terrain *pterrain);

/* Tile */
City *api_methods_tile_city(Tile *ptile);
bool api_methods_tile_city_exists_within_max_city_map(Tile *ptile,
                                                      bool may_be_on_center);
int api_methods_tile_num_units(Tile *ptile);
int api_methods_tile_sq_distance(Tile *ptile1, Tile *ptile2);
int api_methods_private_tile_next_outward_index(Tile *pstart, int index,
                                                int max_dist);
Tile *api_methods_private_tile_for_outward_index(Tile *pstart, int index);
Unit_List_Link *api_methods_private_tile_unit_list_head(Tile *ptile);

/* Unit */
bool api_methods_unit_city_can_be_built_here(Unit *punit);

/* Unit Type */
bool api_methods_unit_type_has_flag(Unit_Type *punit_type, const char *flag);
bool api_methods_unit_type_has_role(Unit_Type *punit_type, const char *role);
bool api_methods_unit_type_can_exist_at_tile(Unit_Type *punit_type,
                                             Tile *ptile);
const char *api_methods_unit_type_rule_name(Unit_Type *punit_type);
const char *api_methods_unit_type_name_translation(Unit_Type *punit_type);

/* Unit_List_Link Type */
Unit *api_methods_unit_list_link_data(Unit_List_Link *link);
Unit_List_Link *api_methods_unit_list_next_link(Unit_List_Link *link);

/* City_List_Link Type */
City *api_methods_city_list_link_data(City_List_Link *link);
City_List_Link *api_methods_city_list_next_link(City_List_Link *link);


#endif /* FC__API_METHODS_H */
