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
#ifndef __UNITFUNC_H
#define __UNITFUNC_H

#include "packets.h"
#include "unit.h"


void diplomat_investigate(struct player *pplayer, struct unit *pdiplomat, struct city *pcity);
void spy_poison(struct player *pplayer, struct unit *pdiplomat, 
		struct city *pcity);
void spy_sabotage_unit(struct player *pplayer, struct unit *pdiplomat, struct unit *pvictim);
int diplomat_infiltrate_city(struct player *pplayer, struct player *cplayer,
			     struct unit *pdiplomat, struct city *pcity);
void diplomat_leave_city(struct player *pplayer, struct unit *pdiplomat,
			 struct city *pcity);
void diplomat_bribe(struct player *pplayer, struct unit *pdiplomat, 
		    struct unit *pvictim);
void diplomat_get_tech(struct player *pplayer, struct unit *pdiplomat, 
		       struct city  *pcity, int tech);
void diplomat_incite(struct player *pplayer, struct unit *pdiplomat, 
		     struct city *pcity);
void diplomat_sabotage(struct player *pplayer, struct unit *pdiplomat, 
		       struct city *pcity, int improvement);

void player_restore_units(struct player *pplayer);
void unit_restore_hitpoints(struct player *pplayer, struct unit *punit);
void unit_restore_movepoints(struct player *pplayer, struct unit *punit);
void update_unit_activities(struct player *pplayer);
void update_unit_activity(struct player *pplayer, struct unit *punit);

void create_unit(struct player *pplayer, int x, int y, enum unit_type_id type,
		 int make_veteran, int homecity_id, int moves_left);
void create_unit_full(struct player *pplayer, int x, int y, enum unit_type_id 
		      type, int make_veteran, int homecity_id, int moves_left
		      , int hp);
void send_remove_unit(struct player *pplayer, int unit_id);
void wipe_unit(struct player *dest, struct unit *punit);
void wipe_unit_safe(struct player *dest, struct unit *punit,
		    struct genlist_iterator *iter);
void wipe_unit_spec_safe(struct player *dest, struct unit *punit,
		    struct genlist_iterator *iter, int wipe_cargo);

void kill_unit(struct unit *pkiller, struct unit *punit);
void send_unit_info(struct player *dest, struct unit *punit, int dosend);

void maybe_make_veteran(struct unit *punit);
void unit_versus_unit(struct unit *attacker, struct unit *defender);
int get_total_attack_power(struct unit *attacker, struct unit *defender);
int get_total_defense_power(struct unit *attacker, struct unit *defender);
int get_virtual_defense_power(int a_type, int d_type, int x, int y);
void set_unit_activity(struct unit *punit, enum unit_activity new_activity);
void do_nuke_tile(int x, int y);
void do_nuclear_explosion(int x, int y);
int try_move_unit(struct unit *punit, int dest_x, int dest_y); 
int do_airline(struct unit *punit, int x, int y);
void raze_city(struct city *pcity);
void get_a_tech(struct player *pplayer, struct player *target);
void place_partisans(struct city *pcity,int count);
void make_partisans(struct city *pcity);

#endif



