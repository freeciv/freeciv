/********************************************************************** 
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef __CITYTOOLS_H
#define __CITYTOOLS_H

#include "packets.h"
#include "city.h"

#define FOOD_WEIGHTING 19
#define SHIELD_WEIGHTING 17
#define TRADE_WEIGHTING 8

int city_got_barracks(struct city *pcity);
int can_sell_building(struct city *pcity, int id);
enum city_tile_type get_worker_city(struct city *pcity, int x, int y);
int is_worker_here(struct city *pcity, int x, int y); 
struct city *find_city_wonder(enum improvement_type_id id);
struct city *find_palace(struct player *pplayer);
int city_specialists(struct city *pcity);                 /* elv+tax+scie */
int content_citizens(struct player *pplayer); 
int get_temple_power(struct city *pcity);
int get_cathedral_power(struct city *pcity);
int get_colosseum_power(struct city *pcity);
int is_worked_here(int x, int y);
int can_place_worker_here(struct city *pcity, int x, int y);
int food_weighting(int n);
int city_tile_value(struct city *pcity, int x, int y, int foodneed, int prodneed);
int better_tile(struct city *pcity, int x, int y, int bx, int by, int foodneed, int prodneed);
int best_tile(struct city *pcity, int x, int y, int bx, int by);
int best_food_tile(struct city *pcity, int x, int y, int bx, int by);
int settler_eats(struct city *pcity);
int is_building_other_wonder(struct city *pc);
int built_elsewhere(struct city *pc, int wonder);
void eval_buildings(struct city *pcity,int *values);
int do_make_unit_veteran(struct city *pcity, enum unit_type_id id);
int city_corruption(struct city *pcity, int trade);
int set_city_shield_bonus(struct city *pcity);
int city_shield_bonus(struct city *pcity);
int set_city_science_bonus(struct city *pcity);
int city_science_bonus(struct city *pcity);
int set_city_tax_bonus(struct city *pcity);
int city_tax_bonus(struct city *pcity);
int wants_to_be_bigger(struct city *pcity);
int worst_worker_tile_value(struct city *pcity);
#endif









