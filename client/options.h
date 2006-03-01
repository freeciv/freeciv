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

#include "events.h"
#include "shared.h"		/* bool type */

extern char default_user_name[512];
extern char default_server_host[512];
extern int default_server_port; 
extern char default_metaserver[512];
extern char default_tileset_name[512];
extern char default_sound_set_name[512];
extern char default_sound_plugin_name[512];

/** Local Options: **/

extern bool solid_color_behind_units;
extern bool sound_bell_at_new_turn;
extern int smooth_move_unit_msec;
extern int smooth_center_slide_msec;
extern bool do_combat_animation;
extern bool ai_popup_windows;
extern bool ai_manual_turn_done;
extern bool auto_center_on_unit;
extern bool auto_center_on_combat;
extern bool wakeup_focus;
extern bool goto_into_unknown;
extern bool center_when_popup_city;
extern bool concise_city_production;
extern bool auto_turn_done;
extern bool meta_accelerators;
extern bool map_scrollbars;
extern bool dialogs_on_top;
extern bool ask_city_name;
extern bool popup_new_cities;
extern bool update_city_text_in_refresh_tile;
extern bool keyboardless_goto;
extern bool show_task_icons;

enum client_option_type {
  COT_BOOL,
  COT_INT,
  COT_STR
};

typedef struct client_option {
  const char *name;
  const char *description;
  enum client_option_type type;
  int *p_int_value;
  bool *p_bool_value;
  char *p_string_value;
  size_t string_length;
  void (*change_callback) (struct client_option * option);

  /* 
   * A function to return a static NULL-terminated list of possible
   * string values, or NULL for none. 
   */
  const char **(*p_string_vals)(void);

  /* volatile */
  void *p_gui_data;
} client_option;
extern client_option *options;

#define GEN_INT_OPTION(oname, desc) { #oname, desc, COT_INT, \
                                      &oname, NULL, NULL, 0, NULL, \
                                       NULL, NULL }
#define GEN_BOOL_OPTION(oname, desc) { #oname, desc, COT_BOOL, \
                                       NULL, &oname, NULL, 0, NULL, \
                                       NULL, NULL }
#define GEN_STR_OPTION(oname, desc, str_defaults, callback) \
                                    { #oname, desc, COT_STR, \
                                      NULL, NULL, oname, sizeof(oname), \
                                      callback, str_defaults, NULL }

extern int num_options;

#define client_options_iterate(o)                                           \
{                                                                           \
  int _i;                                                                   \
  for (_i = 0; _i < num_options; _i++) {                                    \
    client_option *o = options + _i;                                        \
    {

#define client_options_iterate_end                                          \
    }                                                                       \
  }                                                                         \
}

/* GUI-specific options declared in gui-xxx but handled by common code. */
extern const int num_gui_options;
extern client_option gui_options[];

/** View Options: **/

extern bool draw_map_grid;
extern bool draw_city_names;
extern bool draw_city_growth;
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
extern bool draw_borders;

typedef struct {
  const char *name;
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
const char *get_message_text(enum event_type event);

void init_messages_where(void);

void load_general_options(void);
void load_ruleset_specific_options(void);
void save_options(void);
const char *get_sound_tag_for_event(enum event_type event);
bool is_city_event(enum event_type event);

#endif  /* FC__OPTIONS_H */
