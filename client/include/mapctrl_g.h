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
#ifndef FC__MAPCTRL_G_H
#define FC__MAPCTRL_G_H

struct unit;
struct city;

extern struct city *city_workers_display;

void popup_newcity_dialog(struct unit *punit, char *suggestname);

void set_turn_done_button_state( int state );

#endif  /* FC__MAPCTRL_G_H */
