/**********************************************************************
 Freeciv - Copyright (C) 2002 - The Freeciv Poject
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__MAPCTRL_COMMON_H
#define FC__MAPCTRL_COMMON_H

#include "map.h"		/* enum direction8 */
#include "shared.h"		/* bool type */

bool get_turn_done_button_state(void);
void scroll_mapview(enum direction8 gui_dir);
void wakeup_button_pressed(int canvas_x, int canvas_y);
void adjust_workers_button_pressed(int canvas_x, int canvas_y);
void recenter_button_pressed(int canvas_x, int canvas_y);
void update_turn_done_button_state(void);
void update_line(int canvas_x, int canvas_y);

extern struct city *city_workers_display;

#endif /* FC__MAPVIEW_COMMON_H */
