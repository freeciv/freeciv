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
#ifndef __CITYDLG__H
#define __CITYDLG__H

struct city_dialog;

void activate_unit(struct unit *punit);
void popup_city_dialog(struct city *pcity, int make_modal);
void popdown_city_dialog(struct city *pcity);
void button_down_citymap(Widget w, XEvent *event, String *argv, Cardinal *argc);
void refresh_city_dialog(struct city *pcity);
void refresh_unit_city_dialogs(struct unit *punit);
void close_city_dialog_action(Widget w, XEvent *event, String *argv, Cardinal *argc);
void city_dialog_returnkey(Widget w, XEvent *event, String *params, Cardinal *num_params);
#endif
