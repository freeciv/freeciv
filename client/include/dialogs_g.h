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
#ifndef FC__DIALOGS_G_H
#define FC__DIALOGS_G_H

#include "shared.h"		/* bool type */

struct tile;
struct unit;
struct city;
struct packet_nations_used;

void popup_notify_goto_dialog(char *headline, char *lines, int x, int y);
void popup_notify_dialog(char *caption, char *headline, char *lines);

void popup_races_dialog(void);
void popdown_races_dialog(void);

void popup_unit_select_dialog(struct tile *ptile);

void races_toggles_set_sensitive(struct packet_nations_used *packet);

void popup_revolution_dialog(void);
void popup_government_dialog(void);
void popup_caravan_dialog(struct unit *punit,
			  struct city *phomecity, struct city *pdestcity);
bool caravan_dialog_is_open(void);
void popup_diplomat_dialog(struct unit *punit, int dest_x, int dest_y);
bool diplomat_dialog_is_open(void);
void popup_incite_dialog(struct city *pcity);
void popup_bribe_dialog(struct unit *punit);
void popup_sabotage_dialog(struct city *pcity);
void popup_pillage_dialog(struct unit *punit, int may_pillage);
void popup_unit_connect_dialog (struct unit *punit, int dest_x, int dest_y);

#endif  /* FC__DIALOGS_G_H */
