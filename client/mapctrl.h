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
#ifndef __MAPCTRL_H
#define __MAPCTRL_H

#include <X11/Intrinsic.h>
#include "unit.h"
#include "packets.h"

struct unit *get_unit_in_focus(void);
void advance_unit_focus(void);
void update_unit_focus(void);

void request_unit_goto(void);
void request_unit_move_done(void);
void request_unit_build_city(struct unit *punit);
void request_unit_disband(struct unit *punit);
void request_unit_change_homecity(struct unit *punit);
void request_unit_auto(struct unit *punit);
void request_unit_unload(struct unit *punit);

void request_new_unit_activity(struct unit *punit, enum unit_activity act);

void butt_down_mapcanvas(Widget w, XEvent *event, String *argv, Cardinal *argc);
void set_unit_focus(struct unit *punit);
void set_unit_focus_no_center(struct unit *punit);

void focus_to_next_unit(Widget w, XEvent *event, String *argv, 
			Cardinal *argc);
void butt_down_overviewcanvas(Widget w, XEvent *event, String *argv, 
			      Cardinal *argc);
void key_unit_unload(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_fortify(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_goto(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_sentry(Widget w, XEvent *event, String *argv, Cardinal *argc);
void request_unit_wait(struct unit *punit);
void key_unit_wait(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_done(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_build_city(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_homecity(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_pillage(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_auto(Widget w, XEvent *event, String *argv, Cardinal *argc);

void key_unit_mine(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_road(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_irrigate(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_terraform(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_north(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_north_east(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_east(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_south_east(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_south(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_south_west(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_west(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_north_west(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_disband(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_end_turn(Widget w, XEvent *event, String *argv, Cardinal *argc);
void center_on_unit(Widget w, XEvent *event, String *argv, Cardinal *argc);
void key_unit_clean_pollution(Widget w, XEvent *event, String *argv, Cardinal *argc);
void do_move_unit(struct unit *punit, struct packet_unit_info *pinfo);

void city_new_name_return(Widget w, XEvent *event, String *params,
			  Cardinal *num_params);
void popupinfo_popdown_callback(Widget w, XtPointer client_data,
				XtPointer call_data);

#endif
