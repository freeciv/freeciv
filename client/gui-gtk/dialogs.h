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

void popup_notify_goto_dialog(char *headline, char *lines,int x, int y);
void popup_notify_dialog(char *headline, char *lines);
void message_dialog_button_set_sensitive(GtkWidget *shl, char *bname, int state);
GtkWidget *popup_message_dialog(GtkWidget *parent, char *shellname, char *text,
				...);
void destroy_message_dialog(GtkWidget *button);

struct tile;
struct unit;
struct city;

void popup_races_dialog(void);
void popdown_races_dialog(void);
void popup_unit_select_dialog(struct tile *ptile);

void races_toggles_set_sensitive(int bits);

void popup_revolution_dialog(void);
void popup_government_dialog(void);
void popup_caravan_dialog(struct unit *punit,
			  struct city *phomecity, struct city *pdestcity);
void popup_diplomat_dialog(struct unit *punit, int dest_x, int dest_y);
void popup_incite_dialog(struct city *pcity);
void popup_bribe_dialog(struct unit *punit);

void destroy_me_callback(GtkWidget *w, gpointer data);
void taxrates_callback(GtkWidget *w, GdkEventButton *ev, gpointer data);

gint deleted_callback(GtkWidget *w, GdkEvent *ev, gpointer data);
#endif
