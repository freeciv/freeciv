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
#ifndef __UNITTOOLS_H
#define __UNITTOOLS_H
#include "packets.h"
#include "unit.h"

int can_unit_move_to_tile(struct unit *punit, int x, int y);
int can_unit_see_tile(struct unit *punit, int x, int y);
int is_sailing_unit_tile(int x, int y);
int is_my_zoc(struct unit *myunit, int x0, int y0);
int zoc_ok_move(struct unit *punit,int x, int y);
int unit_bribe_cost(struct unit *punit);
int diplomat_on_tile(int x, int y);
int hp_gain_coord(struct unit *punit);
int rate_unit(struct unit *punit, struct unit *against);
struct unit *get_defender(struct player *pplayer, struct unit *aunit, 
			  int x, int y);
int get_attack_power(struct unit *punit);
int get_defense_power(struct unit *punit);
int unit_ignores_citywalls(struct unit *punit);
int unit_behind_walls(struct unit *punit);
int unit_on_fortress(struct unit *punit);
int unit_behind_coastal(struct unit *punit);
int unit_behind_sam(struct unit *punit);
int unit_behind_sdi(struct unit *punit);
struct city *sdi_defense_close(int owner, int x, int y);
int find_a_unit_type();
int can_unit_attack_tile(struct unit *punit, int dest_x, int dest_y);
int build_points_left(struct city *pcity);
int can_place_partisan(int x, int y);
int is_already_assigned(struct unit *myunit, struct player *pplayer, 
			int x, int y);
int benefit_pollution(struct player *pplayer, int x, int y);
int benefit_road(struct player *pplayer, int x, int y);
int benefit_mine(struct player *pplayer, int x, int y);
int benefit_irrigate(struct player *pplayer, int x, int y);
int ai_calc_pollution(struct unit *punit, struct player *pplayer, 
		      int x, int y);
int ai_calc_mine(struct unit *punit, struct player *pplayer, int x, int y);
int ai_calc_road(struct unit *punit, struct player *pplayer, int x, int y);
int get_food_tile_bc(int xp, int yp);
int is_ok_city_spot(int x, int y);
int in_city_radius(struct player *pplayer, int x, int y);
int ai_calc_irrigate(struct unit *punit, struct player *pplayer, int x, int y);
int dist_mod(int dist, int val);
int make_dy(int y1, int y2);
int make_dx(int x1, int x2);







#endif





