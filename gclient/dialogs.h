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
#ifndef __DIALOGS_H
#define __DIALOGS_H

struct tile;
struct unit;
struct city;

void destroy_me_callback( GtkWidget *w, gpointer data);
void races_toggles_set_sensitive(int bits);
void popup_notify_dialog(char *headline, char *lines);

void popup_races_dialog(void);
void popdown_races_dialog(void);

#endif
