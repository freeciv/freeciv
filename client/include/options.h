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
#ifndef FC__OPTIONS_H
#define FC__OPTIONS_H

extern int use_solid_color_behind_units;
extern int sound_bell_at_new_turn;
extern int smooth_move_units;
extern int flags_are_transparent;
extern int ai_popup_windows;
extern int ai_manual_turn_done;
extern int auto_center_on_unit;
extern int wakeup_focus;
extern int draw_diagonal_roads;
extern int center_when_popup_city;
extern int draw_map_grid;

typedef struct {
	char *name;
	char *description;
	int  *p_value;

	/* volatile */
	void *p_gui_data;
} client_option;
extern client_option options[];

#define GEN_OPTION(name, description) { #name, description, &name, NULL }
#define NULL_OPTION { NULL, NULL, NULL, NULL }

void load_options(void);
void save_options(void);

#endif  /* FC__OPTIONS_H */
