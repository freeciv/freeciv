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

/** Local Options: **/

extern int solid_color_behind_units;
extern int sound_bell_at_new_turn;
extern int smooth_move_units;
extern int smooth_move_unit_steps;
extern int do_combat_animation;
extern int ai_popup_windows;
extern int ai_manual_turn_done;
extern int auto_center_on_unit;
extern int auto_center_on_combat;
extern int wakeup_focus;
extern int draw_diagonal_roads;
extern int center_when_popup_city;
extern int concise_city_production;
extern int auto_turn_done;

enum client_option_type {
  COT_BOOL,
  COT_INT
};
typedef struct {
	char *name;
	char *description;
	enum client_option_type type;
	int  *p_value;

	/* volatile */
	void *p_gui_data;
} client_option;
extern client_option options[];

/** View Options: **/

extern int draw_map_grid;
extern int draw_city_names;
extern int draw_city_productions;
extern int draw_terrain;
extern int draw_coastline;
extern int draw_roads_rails;
extern int draw_irrigation;
extern int draw_mines;
extern int draw_fortress_airbase;
extern int draw_specials;
extern int draw_pollution;
extern int draw_cities;
extern int draw_units;
extern int draw_focus_unit;
extern int draw_fog_of_war;

typedef struct {
	char *name;
	int  *p_value;
} view_option;
extern view_option view_options[];

/** Message Options: **/

/* for specifying which event messages go where: */
#define NUM_MW 3
#define MW_OUTPUT    1		/* add to the output window */
#define MW_MESSAGES  2		/* add to the messages window */
#define MW_POPUP     4		/* popup an individual window */

extern unsigned int messages_where[];	/* OR-ed MW_ values [E_LAST] */
extern const char *message_text[];      /* [E_LAST] */
extern int sorted_events[];	        /* [E_LAST], sorted by message_text */


void init_messages_where(void);

void load_options(void);
void save_options(void);

#endif  /* FC__OPTIONS_H */
