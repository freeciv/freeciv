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
#ifndef FC__CONTROL_H
#define FC__CONTROL_H

#ifndef FC__PACKETS_H
#include "packets.h"
#endif

void do_move_unit(struct unit *punit, struct packet_unit_info *pinfo);
void do_unit_nuke(struct unit *punit);

void request_center_focus_unit(void);
void request_move_unit_direction(struct unit *punit, int dx, int dy); /* used by key_xxx */
void request_new_unit_activity(struct unit *punit, enum unit_activity act);
void request_unit_auto(struct unit *punit);
void request_unit_build_city(struct unit *punit);
void request_unit_caravan_action(struct unit *punit, enum packet_type action);
void request_unit_change_homecity(struct unit *punit);
void request_unit_disband(struct unit *punit);
void request_unit_goto(void);
void request_unit_move_done(void);
void request_unit_nuke(struct unit *punit);
void request_unit_unload(struct unit *punit);
void request_unit_upgrade(struct unit *punit);
void request_unit_wait(struct unit *punit);
void request_unit_wakeup(struct unit *punit);
void request_toggle_map_grid(void);

void wakeup_sentried_units(int x, int y);

void advance_unit_focus(void);
struct unit *get_unit_in_focus(void);
void set_unit_focus(struct unit *punit);
void set_unit_focus_no_center(struct unit *punit);
void update_unit_focus(void);

void key_end_turn(void);
void key_map_grid(void);
void key_unit_auto(void);
void key_unit_build_city(void);
void key_unit_clean_pollution(void);
void key_unit_disband(void);
void key_unit_done(void);
void key_unit_east(void);
void key_unit_explore(void);
void key_unit_fortify(void);
void key_unit_goto(void);
void key_unit_homecity(void);
void key_unit_irrigate(void);
void key_unit_mine(void);
void key_unit_north_east(void);
void key_unit_north_west(void);
void key_unit_north(void);
void key_unit_nuke(void);
void key_unit_pillage(void);
void key_unit_road(void);
void key_unit_sentry(void);
void key_unit_south_east(void);
void key_unit_south_west(void);
void key_unit_south(void);
void key_unit_transform(void);
void key_unit_unload(void);
void key_unit_wait(void);
void key_unit_wakeup(void);
void key_unit_west(void);

#endif  /* FC__CONTROL_H */

