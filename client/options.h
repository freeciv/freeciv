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

/* utility */
#include "support.h"            /* bool type */

/* common */
#include "events.h"
#include "fc_types.h"           /* enum gui_type */

extern char default_user_name[512];
extern char default_server_host[512];
extern int default_server_port; 
extern char default_metaserver[512];
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
extern bool auto_center_each_turn;
extern bool wakeup_focus;
extern bool goto_into_unknown;
extern bool center_when_popup_city;
extern bool concise_city_production;
extern bool auto_turn_done;
extern bool meta_accelerators;
extern bool ask_city_name;
extern bool popup_new_cities;
extern bool popup_caravan_arrival;
extern bool update_city_text_in_refresh_tile;
extern bool keyboardless_goto;
extern bool enable_cursor_changes;
extern bool separate_unit_selection;
extern bool unit_selection_clears_orders;
extern char highlight_our_names[128];

extern bool voteinfo_bar_use;
extern bool voteinfo_bar_always_show;
extern bool voteinfo_bar_hide_when_not_player;
extern bool voteinfo_bar_new_at_front;

extern bool draw_city_outlines;
extern bool draw_city_output;
extern bool draw_map_grid;
extern bool draw_city_names;
extern bool draw_city_growth;
extern bool draw_city_productions;
extern bool draw_city_buycost;
extern bool draw_city_trade_routes;
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

/* gui-gtk-2.0 client specific options. */
#define FC_GTK_DEFAULT_THEME_NAME "Freeciv"
extern char gui_gtk2_default_theme_name[512];
extern bool gui_gtk2_map_scrollbars;
extern bool gui_gtk2_dialogs_on_top;
extern bool gui_gtk2_show_task_icons;
extern bool gui_gtk2_enable_tabs;
extern bool gui_gtk2_better_fog;
extern bool gui_gtk2_show_chat_message_time;
extern bool gui_gtk2_split_bottom_notebook;
extern bool gui_gtk2_new_messages_go_to_top;
extern bool gui_gtk2_show_message_window_buttons;
extern bool gui_gtk2_metaserver_tab_first;
extern bool gui_gtk2_allied_chat_only;
extern bool gui_gtk2_small_display_layout;
extern bool gui_gtk2_mouse_over_map_focus;
extern char gui_gtk2_font_city_label[512];
extern char gui_gtk2_font_notify_label[512];
extern char gui_gtk2_font_spaceship_label[512];
extern char gui_gtk2_font_help_label[512];
extern char gui_gtk2_font_help_link[512];
extern char gui_gtk2_font_help_text[512];
extern char gui_gtk2_font_chatline[512];
extern char gui_gtk2_font_beta_label[512];
extern char gui_gtk2_font_small[512];
extern char gui_gtk2_font_comment_label[512];
extern char gui_gtk2_font_city_names[512];
extern char gui_gtk2_font_city_productions[512];
extern char gui_gtk2_font_reqtree_text[512];

/* gui-sdl client specific options. */
#define FC_SDL_DEFAULT_THEME_NAME "human"
extern char gui_sdl_default_theme_name[512];
extern bool gui_sdl_fullscreen;
extern int gui_sdl_screen_width;
extern int gui_sdl_screen_height;

/* gui-win32 client specific options. */
extern bool gui_win32_better_fog;
extern bool gui_win32_enable_alpha;


enum option_type {
  OT_BOOLEAN,
  OT_INTEGER,
  OT_STRING,
  OT_FONT
};

struct option;                  /* Opaque type. */


/* Main functions. */
void options_init(void);
void options_free(void);
void options_load(void);
void options_save(void);


/* Common option functions. */
int option_number(const struct option *poption);
const char *option_name(const struct option *poption);
const char *option_description(const struct option *poption);
const char *option_help_text(const struct option *poption);
enum option_type option_type(const struct option *poption);
int option_category(const struct option *poption);
struct option *option_next(const struct option *poption);

bool option_reset(struct option *poption);
void option_set_changed_callback(struct option *poption,
                                 void (*callback) (struct option *));
void option_changed(struct option *poption);

/* Option gui functions. */
void option_set_gui_data(struct option *poption, void *data);
void *option_get_gui_data(const struct option *poption);

/* Option type COT_BOOLEAN functions. */
bool option_bool_get(const struct option *poption);
bool option_bool_def(const struct option *poption);
bool option_bool_set(struct option *poption, bool val);

/* Option type COT_INTEGER functions. */
int option_int_get(const struct option *poption);
int option_int_def(const struct option *poption);
int option_int_min(const struct option *poption);
int option_int_max(const struct option *poption);
bool option_int_set(struct option *poption, int val);

/* Option type COT_STRING functions. */
const char *option_str_get(const struct option *poption);
const char *option_str_def(const struct option *poption);
const struct strvec *option_str_values(const struct option *poption);
bool option_str_set(struct option *poption, const char *str);

/* Option type COT_FONT functions. */
const char *option_font_get(const struct option *poption);
const char *option_font_def(const struct option *poption);
const char *option_font_target(const struct option *poption);
bool option_font_set(struct option *poption, const char *font);


/* Client options function accessors. */
struct option *client_option_by_number(int id);
struct option *client_option_by_name(const char *name);
struct option *client_option_first(void);

int client_option_category_number(void);
const char *client_option_category_name(int category);

#define client_options_iterate(poption)                                     \
{                                                                           \
  struct option *poption = client_option_first();                           \
  for (; NULL != poption; poption = option_next(poption)) {                 \

#define client_options_iterate_end                                          \
  }                                                                         \
}


/** Desired settable options. **/
struct options_settable;
void desired_settable_options_update(void);
void desired_settable_option_update(const char *op_name,
                                    const char *op_value,
                                    bool allow_replace);
void desired_settable_option_send(struct options_settable *pset);


/** Dialog report options. **/
void options_dialogs_update(void);
void options_dialogs_set(void);


/** Message Options: **/

/* for specifying which event messages go where: */
#define NUM_MW 3
#define MW_OUTPUT    1		/* add to the output window */
#define MW_MESSAGES  2		/* add to the messages window */
#define MW_POPUP     4		/* popup an individual window */

extern int messages_where[];	/* OR-ed MW_ values [E_LAST] */

#endif  /* FC__OPTIONS_H */
