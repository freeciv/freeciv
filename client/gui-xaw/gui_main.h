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
#ifndef FC__GUI_MAIN_H
#define FC__GUI_MAIN_H

#include <X11/Intrinsic.h>

void ui_main(int argc, char *argv[]);
void setup_widgets(void);

void quit_civ(Widget w, XtPointer client_data, XtPointer call_data);
void end_turn_callback(Widget w, XtPointer client_data, XtPointer call_data);
void remove_net_callback(void);
void remove_net_input(void);
void enable_turn_done_button(void);

#endif  /* FC__GUI_MAIN_H */
