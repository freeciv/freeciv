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
#ifndef FC__GUI_MAIN_G_H
#define FC__GUI_MAIN_G_H

struct unit;

void ui_main(int argc, char *argv[]);
void sound_bell(void);
void enable_turn_done_button(void);
void add_net_input(int);
void remove_net_input(void);

void set_unit_icon(int idx, struct unit *punit);
void set_unit_icons_more_arrow(int onoff);

#endif  /* FC__GUI_MAIN_G_H */
