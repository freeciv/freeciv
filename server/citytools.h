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
#include "nation.h" /* for struct city_name */

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
#define WARMING_FACTOR 50

bool city_got_barracks(struct city *pcity);
bool can_sell_building(struct city *pcity, int id);
struct city *find_city_wonder(Impr_Type_id id);
int build_points_left(struct city *pcity);
bool is_worked_here(int x, int y);
int food_weighting(int city_size);
int city_tile_value(struct city *pcity, int x, int y, int foodneed, int prodneed);
int settler_eats(struct city *pcity);
bool is_building_other_wonder(struct city *pc);
bool built_elsewhere(struct city *pc, int wonder);
bool do_make_unit_veteran(struct city *pcity, Unit_Type_id id);
int city_shield_bonus(struct city *pcity);
int city_science_bonus(struct city *pcity);
int city_tax_bonus(struct city *pcity);

bool wants_to_be_bigger(struct city *pcity);
int worst_worker_tile_value(struct city *pcity);
int best_worker_tile_value(struct city *pcity);
void transfer_city_units(struct player *pplayer, struct player *pvictim, 
			 struct unit_list *units, struct city *pcity,
			 struct city *exclude_city,
			 int kill_outside, bool verbose);
void transfer_city(struct player *ptaker, struct city *pcity,
		   int kill_outside, bool transfer_unit_verbose,
		   bool resolve_stack, bool raze);
struct city *find_closest_owned_city(struct player *pplayer, int x, int y,
				     bool sea_required, struct city *pexclcity);
void handle_unit_enter_city(struct unit *punit, struct city *pcity);

void send_city_info(struct player *dest, struct city *pcity);
void send_city_info_at_tile(struct player *pviewer, struct conn_list *dest,
			    struct city *pcity, int x, int y);
void send_all_known_cities(struct conn_list *dest);
void send_player_cities(struct player *pplayer);
void package_city(struct city *pcity, struct packet_city_info *packet,
		  bool dipl_invest);

void reality_check_city(struct player *pplayer,int x, int y);
void update_dumb_city(struct player *pplayer, struct city *pcity);

void create_city(struct player *pplayer, const int x, const int y, char *name);
void remove_city(struct city *pcity);

int establish_trade_route(struct city *pc1, struct city *pc2);
void remove_trade_route(struct city *pc1, struct city *pc2);

void do_sell_building(struct player *pplayer, struct city *pcity, int id);
void building_lost(struct city *pcity, int id);
void change_build_target(struct player *pplayer, struct city *pcity, 
			 int target, bool is_unit, int event);

char *city_name_suggestion(struct player *pplayer, int x, int y);
extern struct city_name *misc_city_names;


bool city_can_work_tile(struct city *pcity, int city_x, int city_y);
void server_remove_worker_city(struct city *pcity, int city_x, int city_y);
void server_set_worker_city(struct city *pcity, int city_x, int city_y);
void update_city_tile_status_map(struct city *pcity, int map_x, int map_y);
void update_city_tile_status(struct city *pcity, int city_x, int city_y);
void sync_cities(void);
bool can_place_worker_here(struct city *pcity, int city_x, int city_y);
void check_city_workers(struct player *pplayer);

#endif  /* FC__CITYTOOLS_H */
