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
#ifndef FC__CITYTOOLS_H
#define FC__CITYTOOLS_H

#include "packets.h"
#include "city.h"

#define FOOD_WEIGHTING 19
#define SHIELD_WEIGHTING 17
#define TRADE_WEIGHTING 12
/* The Trade Weighting has to about as large as the Shield Weighting,
   otherwise the AI will build Barracks to create veterans in cities 
   with only 1 shields production.
    8 is too low
   18 is too high
 */
#define POLLUTION_WEIGHTING 14 /* tentative */
#define WARMING_FACTOR 32 /* tentative */

int city_got_barracks(struct city *pcity);
int can_sell_building(struct city *pcity, int id);
struct city *find_city_wonder(Impr_Type_id id);
int city_specialists(struct city *pcity);                 /* elv+tax+scie */
int content_citizens(struct player *pplayer); 
int get_temple_power(struct city *pcity);
int get_cathedral_power(struct city *pcity);
int get_colosseum_power(struct city *pcity);
int build_points_left(struct city *pcity);
int in_city_radius(int x, int y);
int is_worked_here(int x, int y);
int can_place_worker_here(struct city *pcity, int x, int y);
int food_weighting(int city_size);
int city_tile_value(struct city *pcity, int x, int y, int foodneed, int prodneed);
int settler_eats(struct city *pcity);
int is_building_other_wonder(struct city *pc);
int built_elsewhere(struct city *pc, int wonder);
int do_make_unit_veteran(struct city *pcity, Unit_Type_id id);
int city_corruption(struct city *pcity, int trade);
int set_city_shield_bonus(struct city *pcity);
int city_shield_bonus(struct city *pcity);
int set_city_science_bonus(struct city *pcity);
int city_science_bonus(struct city *pcity);
int set_city_tax_bonus(struct city *pcity);
int city_tax_bonus(struct city *pcity);
int wants_to_be_bigger(struct city *pcity);
int worst_worker_tile_value(struct city *pcity);
int best_worker_tile_value(struct city *pcity);
void transfer_city_units(struct player *pplayer, struct player *pvictim, 
			 struct city *pcity, struct city *vcity,
			 int kill_outside, int verbose);
int civil_war_triggered(struct player *pplayer);
void civil_war(struct player *pplayer);
struct city *transfer_city(struct player *pplayer, struct player *cplayer,
			   struct city *pcity, int kill_outside,
			   int transfer_unit_verbose, int resolve_stack, int raze);
struct city *find_closest_owned_city(struct player *pplayer, int x, int y,
				     int sea_required, struct city *pexclcity);

void adjust_city_free_cost(int *num_free, int *this_cost);

#endif  /* FC__CITYTOOLS_H */
