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
extern char default_theme_name[512];
extern char default_tileset_name[512];
extern char default_sound_set_name[512];
extern char default_sound_plugin_name[512];

extern bool save_options_on_exit;
extern bool fullscreen_mode;

/** Local Options: **/

extern bool solid_color_behind_units;
extern bool sound_bell_at_new_turn;
extern int smooth_move_unit_msec;
extern int smooth_center_slide_msec;
extern bool do_combat_animation;
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

extern char font_city_label[512];
extern char font_notify_label[512];
extern char font_spaceship_label[512];
extern char font_help_label[512];
extern char font_help_link[512];
extern char font_help_text[512];
extern char font_chatline[512];
extern char font_beta_label[512];
extern char font_small[512];
extern char font_comment_label[512];
extern char font_city_names[512];
extern char font_city_productions[512];

enum client_option_type {
  COT_BOOL,
  COT_INT,
  COT_STR,
  COT_FONT
};

enum client_option_class {
  COC_GRAPHICS,
  COC_OVERVIEW,
  COC_SOUND,
  COC_INTERFACE,
  COC_NETWORK,
  COC_FONT,
  COC_MAX
};

extern const char *client_option_class_names[];

typedef struct client_option {
  const char *name; /* Short name - used as an identifier */
  const char *description; /* One-line description */
  const char *helptext; /* Paragraph-length help text */
  enum client_option_class category;
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

#define GEN_INT_OPTION(oname, desc, help, category)			    \
  { #oname, desc, help, category, COT_INT,				    \
      &oname, NULL, NULL, 0, NULL, NULL, NULL }
#define GEN_BOOL_OPTION(oname, desc, help, category)	                    \
  GEN_BOOL_OPTION_CB(oname, desc, help, category, NULL)
#define GEN_BOOL_OPTION_CB(oname, desc, help, category, callback)	    \
  { #oname, desc, help, category, COT_BOOL,				    \
      NULL, &oname, NULL, 0, callback, NULL, NULL }
#define GEN_STR_OPTION(oname, desc, help, category, str_defaults, callback) \
  { #oname, desc, help, category, COT_STR,			    \
      NULL, NULL, oname, sizeof(oname), callback, str_defaults, NULL }
#define GEN_FONT_OPTION(value, oname, desc, help, category) \
  { #oname, desc, help, category, COT_FONT,		    \
      NULL, NULL, value, sizeof(value), NULL, NULL, NULL }

/* Initialization and iteration */
struct client_option *client_option_array_first(void);
const struct client_option *client_option_array_last(void);

#define client_options_iterate(_p)					\
{									\
  struct client_option *_p = client_option_array_first();		\
  if (NULL != _p) {							\
    for (; _p <= client_option_array_last(); _p++) {

#define client_options_iterate_end					\
    }									\
  }									\
}

/* GUI-specific options declared in gui-xxx but handled by common code. */
extern const int num_gui_options;
extern client_option gui_options[];

/** View Options: **/

extern bool draw_city_outlines;
extern bool draw_city_output;
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
extern bool draw_full_citybar;
extern bool draw_unit_shields;

extern bool player_dlg_show_dead_players;
extern bool reqtree_show_icons;
extern bool reqtree_curved_lines;

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

void message_options_init(void);
void message_options_free(void);

void load_general_options(void);
void load_ruleset_specific_options(void);
void load_settable_options(bool send_it);
void save_options(void);

/* Callback functions for changing options. */
void mapview_redraw_callback(struct client_option *option);

#endif  /* FC__OPTIONS_H */
