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

#include "shared.h"		/* bool type */
#include "events.h"

#include "events.h"

/** Local Options: **/

extern bool solid_color_behind_units;
extern bool sound_bell_at_new_turn;
extern bool smooth_move_units;
extern int smooth_move_unit_steps;
extern bool do_combat_animation;
extern bool ai_popup_windows;
extern bool ai_manual_turn_done;
extern bool auto_center_on_unit;
extern bool auto_center_on_combat;
extern bool wakeup_focus;
extern bool draw_diagonal_roads;
extern bool center_when_popup_city;
extern bool concise_city_production;
extern bool auto_turn_done;
extern bool meta_accelerators;

enum client_option_type {
  COT_BOOL,
  COT_INT
};

typedef struct {
  char *name;
  char *description;
  enum client_option_type type;
  int *p_int_value;
  bool *p_bool_value;

  /* volatile */
  void *p_gui_data;
} client_option;
extern client_option options[];

/** View Options: **/

extern bool draw_map_grid;
extern bool draw_city_names;
extern bool draw_city_productions;
extern bool draw_terrain;
extern bool draw_coastline;
extern bool draw_roads_rails;
extern bool draw_irrigation;
extern bool draw_mines;
extern bool draw_fortress_airbase;
extern bool draw_specials;
extern bool draw_pollution;
extern bool draw_cities;
extern bool draw_units;
extern bool draw_focus_unit;
extern bool draw_fog_of_war;

typedef struct {
	char *name;
	bool *p_value;
} view_option;
extern view_option view_options[];

/** Message Options: **/

/* for specifying which event messages go where: */
#define NUM_MW 3
#define MW_OUTPUT    1		/* add to the output window */
#define MW_MESSAGES  2		/* add to the messages window */
#define MW_POPUP     4		/* popup an individual window */

extern unsigned int messages_where[];	/* OR-ed MW_ values [E_LAST] */
extern int sorted_events[];	        /* [E_LAST], sorted by the
					   translated message text */
const char *const get_message_text(enum event_type event);

void init_messages_where(void);

void load_options(void);
void save_options(void);
const char *const get_sound_tag_for_event(enum event_type event);
bool is_city_event(enum event_type event);

#endif  /* FC__OPTIONS_H */
