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

/* battle related */
int find_a_unit_type(int role, int role_tech);
int can_unit_attack_unit_at_tile(struct unit *punit, struct unit *pdefender, int dest_x, int dest_y);
int can_unit_attack_tile(struct unit *punit, int dest_x, int dest_y);
void maybe_make_veteran(struct unit *punit);
void unit_versus_unit(struct unit *attacker, struct unit *defender);


/* move check related */
int can_unit_move_to_tile_with_notify(struct unit *punit, int dest_x,
				      int dest_y, int igzoc);


/* turn update related */
void player_restore_units(struct player *pplayer);
void update_unit_activities(struct player *pplayer);
int hp_gain_coord(struct unit *punit);


/* various */
void advance_unit_focus(struct unit *punit);
char *get_location_str_in(struct player *pplayer, int x, int y);
char *get_location_str_at(struct player *pplayer, int x, int y);
enum goto_move_restriction get_activity_move_restriction(enum unit_activity activity);
void make_partisans(struct city *pcity);
int enemies_at(struct unit *punit, int x, int y);
int teleport_unit_to_city(struct unit *punit, struct city *pcity, int move_cost,
			  int verbose);
void resolve_unit_stack(int x, int y, int verbose);
int get_watchtower_vision(struct unit *punit);
int unit_profits_of_watchtower(struct unit *punit);


/* creation/deletion/upgrading */
void upgrade_unit(struct unit *punit, Unit_Type_id to_unit);
struct unit *create_unit(struct player *pplayer, int x, int y, Unit_Type_id type,
			 int make_veteran, int homecity_id, int moves_left);
struct unit *create_unit_full(struct player *pplayer, int x, int y,
			      Unit_Type_id type, int make_veteran, int homecity_id,
			      int moves_left, int hp_left);
void wipe_unit(struct unit *punit);
void wipe_unit_safe(struct unit *punit, struct genlist_iterator *iter);
void wipe_unit_spec_safe(struct unit *punit, struct genlist_iterator *iter,
			 int wipe_cargo);
void kill_unit(struct unit *pkiller, struct unit *punit);


/* sending to client */
void package_unit(struct unit *punit, struct packet_unit_info *packet,
		  int carried, int select_it, enum unit_info_use packet_use,
		  int info_city_id, int new_serial_num);
void send_unit_info(struct player *dest, struct unit *punit);
void send_unit_info_to_onlookers(struct conn_list *dest, struct unit *punit, 
				 int x, int y, int carried, int select_it);
void send_all_known_units(struct conn_list *dest);


/* doing a unit activity */
void do_nuclear_explosion(struct player *pplayer, int x, int y);
int try_move_unit(struct unit *punit, int dest_x, int dest_y); 
int do_airline(struct unit *punit, struct city *city2);
int do_paradrop(struct unit *punit, int dest_x, int dest_y);
void assign_units_to_transporter(struct unit *ptrans, int take_from_land);
int move_unit(struct unit *punit, int dest_x, int dest_y,
	      int transport_units, int take_from_land, int move_cost);
enum goto_result goto_route_execute(struct unit *punit);

#endif  /* FC__UNITTOOLS_H */
