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
#ifndef __REPODLGS_H
#define __REPODLGS_H

void report_update_delay_on();
void report_update_delay_off();
char *get_report_title(char *report_name);
void update_report_dialogs();

void science_dialog_update(void);
void popup_science_dialog(int make_modal);
void close_science_dialog_action(Widget, XEvent *, String *, Cardinal *);
void trade_report_dialog_update();
void popup_trade_report_dialog(int make_modal);
void close_trade_dialog_action(Widget, XEvent *, String *, Cardinal *);
void activeunits_report_dialog_update();
void popup_activeunits_report_dialog(int make_modal);
void close_activeunits_dialog_action(Widget, XEvent *, String *, Cardinal *);

#endif
