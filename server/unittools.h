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
#ifndef FC__UNITTOOLS_H
#define FC__UNITTOOLS_H

struct city;
struct player;
struct unit;

int can_unit_move_to_tile(struct unit *punit, int x, int y, int igzoc);
int is_enemy_city_tile(int x, int y, int owner);
int is_friendly_city_tile(int x, int y, int owner);
int is_enemy_unit_tile(int x, int y, int owner);
int is_friendly_unit_tile(int x, int y, int owner);
int is_my_zoc(struct unit *myunit, int x0, int y0);
int zoc_ok_move(struct unit *punit,int x, int y);
int zoc_ok_move_gen(struct unit *punit, int x1, int y1, int x2, int y2);
int unit_bribe_cost(struct unit *punit);
int count_diplomats_on_tile(int x, int y);
int hp_gain_coord(struct unit *punit);
int rate_unit_d(struct unit *punit, struct unit *against);
int rate_unit_a(struct unit *punit, struct unit *against);
struct unit *get_defender(struct player *pplayer, struct unit *aunit, 
			  int x, int y);
struct unit *get_attacker(struct player *pplayer, struct unit *aunit, 
			  int x, int y);
int get_attack_power(struct unit *punit);
int get_defense_power(struct unit *punit);
int unit_really_ignores_zoc(struct unit *punit);
int unit_ignores_citywalls(struct unit *punit);
int unit_really_ignores_citywalls(struct unit *punit);
int unit_behind_walls(struct unit *punit);
int unit_on_fortress(struct unit *punit);
int unit_behind_coastal(struct unit *punit);
int unit_behind_sam(struct unit *punit);
int unit_behind_sdi(struct unit *punit);
struct city *sdi_defense_close(int owner, int x, int y);
int find_a_unit_type(int role, int role_tech);
int can_unit_attack_unit_at_tile(struct unit *punit, struct unit *pdefender, int dest_x, int dest_y);
int can_unit_attack_tile(struct unit *punit, int dest_x, int dest_y);
int build_points_left(struct city *pcity);
int can_place_partisan(int x, int y);
int enemies_at(struct unit *punit, int x, int y);
int teleport_unit_to_city(struct unit *punit, struct city *pcity, int mov_cost,
			  int verbose);
struct unit *is_enemy_unit_on_tile(int x, int y, int owner);
void resolve_unit_stack(int x, int y, int verbose);
int is_airunit_refuel_point(int x, int y, int playerid,
			    Unit_Type_id type, int unit_is_on_tile);

#endif  /* FC__UNITTOOLS_H */
