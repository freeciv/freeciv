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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* utility */
#include "support.h"            /* bool type */

/* common */
#include "events.h"
#include "fc_types.h"           /* enum gui_type */
#include "featured_text.h"      /* struct ft_color */
#include "mapimg.h"

#define DEFAULT_METASERVER_OPTION "default"

struct video_mode {
  int width;
  int height;
};
#define VIDEO_MODE(ARG_width, ARG_height) \
    { ARG_width, ARG_height }
/****************************************************************************
  Constructor.
****************************************************************************/
static inline struct video_mode video_mode(int width, int height)
{
  struct video_mode mode = VIDEO_MODE(width, height);
  return mode;
}


extern char default_user_name[512];
extern char default_server_host[512];
extern int default_server_port; 
extern char default_metaserver[512];
extern char default_tileset_name[512];
extern char default_sound_set_name[512];
extern char default_sound_plugin_name[512];
extern char default_chat_logfile[512];

extern bool save_options_on_exit;
extern bool fullscreen_mode;

/** Migrations **/
extern bool gui_gtk3_migrated_from_gtk2;

/** Local Options: **/

extern bool solid_color_behind_units;
extern bool sound_bell_at_new_turn;
extern int smooth_move_unit_msec;
extern int smooth_center_slide_msec;
extern int smooth_combat_step_msec;
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
extern struct ft_color highlight_our_names;

extern bool voteinfo_bar_use;
extern bool voteinfo_bar_always_show;
extern bool voteinfo_bar_hide_when_not_player;
extern bool voteinfo_bar_new_at_front;

extern bool autoaccept_tileset_suggestion;
extern bool autoaccept_soundset_suggestion;

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
extern bool draw_native;
extern bool draw_full_citybar;
extern bool draw_unit_shields;

extern bool player_dlg_show_dead_players;
extern bool reqtree_show_icons;
extern bool reqtree_curved_lines;

/* options for map images */
extern char mapimg_format[64];
extern int mapimg_zoom;
extern bool mapimg_layer[MAPIMG_LAYER_COUNT];
extern char mapimg_filename[512];

/* gui-gtk-2.0 client specific options. */
#define FC_GTK2_DEFAULT_THEME_NAME "Freeciv"
extern char gui_gtk2_default_theme_name[512];
extern bool gui_gtk2_map_scrollbars;
extern bool gui_gtk2_dialogs_on_top;
extern bool gui_gtk2_show_task_icons;
extern bool gui_gtk2_enable_tabs;
extern bool gui_gtk2_better_fog;
extern bool gui_gtk2_show_chat_message_time;
extern bool gui_gtk2_new_messages_go_to_top;
extern bool gui_gtk2_show_message_window_buttons;
extern bool gui_gtk2_metaserver_tab_first;
extern bool gui_gtk2_allied_chat_only;
enum {
  /* Order must match strings in
   * options.c:gui_gtk_message_chat_location_name() */
  GUI_GTK_MSGCHAT_SPLIT,
  GUI_GTK_MSGCHAT_SEPARATE,
  GUI_GTK_MSGCHAT_MERGED
};
extern int gui_gtk2_message_chat_location; /* enum GUI_GTK_MSGCHAT_* */
extern bool gui_gtk2_small_display_layout;
extern bool gui_gtk2_mouse_over_map_focus;
extern bool gui_gtk2_chatline_autocompletion;
extern int gui_gtk2_citydlg_xsize;
extern int gui_gtk2_citydlg_ysize;
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

/* gui-gtk-3.0 client specific options. */
#define FC_GTK3_DEFAULT_THEME_NAME "Freeciv"
extern char gui_gtk3_default_theme_name[512];
extern bool gui_gtk3_map_scrollbars;
extern bool gui_gtk3_dialogs_on_top;
extern bool gui_gtk3_show_task_icons;
extern bool gui_gtk3_enable_tabs;
extern bool gui_gtk3_better_fog;
extern bool gui_gtk3_show_chat_message_time;
extern bool gui_gtk3_new_messages_go_to_top;
extern bool gui_gtk3_show_message_window_buttons;
extern bool gui_gtk3_metaserver_tab_first;
extern bool gui_gtk3_allied_chat_only;
extern int gui_gtk3_message_chat_location; /* enum GUI_GTK_MSGCHAT_* */
extern bool gui_gtk3_small_display_layout;
extern bool gui_gtk3_mouse_over_map_focus;
extern bool gui_gtk3_chatline_autocompletion;
extern int gui_gtk3_citydlg_xsize;
extern int gui_gtk3_citydlg_ysize;
extern char gui_gtk3_font_city_label[512];
extern char gui_gtk3_font_notify_label[512];
extern char gui_gtk3_font_spaceship_label[512];
extern char gui_gtk3_font_help_label[512];
extern char gui_gtk3_font_help_link[512];
extern char gui_gtk3_font_help_text[512];
extern char gui_gtk3_font_chatline[512];
extern char gui_gtk3_font_beta_label[512];
extern char gui_gtk3_font_small[512];
extern char gui_gtk3_font_comment_label[512];
extern char gui_gtk3_font_city_names[512];
extern char gui_gtk3_font_city_productions[512];
extern char gui_gtk3_font_reqtree_text[512];

/* gui-sdl client specific options. */
#define FC_SDL_DEFAULT_THEME_NAME "human"
extern char gui_sdl_default_theme_name[512];
extern bool gui_sdl_fullscreen;
extern struct video_mode gui_sdl_screen;
extern bool gui_sdl_do_cursor_animation;
extern bool gui_sdl_use_color_cursors;

/* gui-win32 client specific options. */
extern bool gui_win32_better_fog;
extern bool gui_win32_enable_alpha;


#define SPECENUM_NAME option_type
#define SPECENUM_VALUE0 OT_BOOLEAN
#define SPECENUM_VALUE1 OT_INTEGER
#define SPECENUM_VALUE2 OT_STRING
#define SPECENUM_VALUE3 OT_ENUM
#define SPECENUM_VALUE4 OT_BITWISE
#define SPECENUM_VALUE5 OT_FONT
#define SPECENUM_VALUE6 OT_COLOR
#define SPECENUM_VALUE7 OT_VIDEO_MODE
#include "specenum_gen.h"


struct option;                  /* Opaque type. */
struct option_set;              /* Opaque type. */


/* Main functions. */
void options_init(void);
void options_free(void);
void server_options_init(void);
void server_options_free(void);
void options_load(void);
void options_save(void);


/* Option sets. */
extern const struct option_set *client_optset;
extern const struct option_set *server_optset;

struct option *optset_option_by_number(const struct option_set *poptset,
                                       int id);
#define optset_option_by_index optset_option_by_number
struct option *optset_option_by_name(const struct option_set *poptset,
                                     const char *name);
struct option *optset_option_first(const struct option_set *poptset);

int optset_category_number(const struct option_set *poptset);
const char *optset_category_name(const struct option_set *poptset,
                                 int category);


/* Common option functions. */
const struct option_set *option_optset(const struct option *poption);
int option_number(const struct option *poption);
#define option_index option_number
const char *option_name(const struct option *poption);
const char *option_description(const struct option *poption);
const char *option_help_text(const struct option *poption);
enum option_type option_type(const struct option *poption);
int option_category(const struct option *poption);
const char *option_category_name(const struct option *poption);
bool option_is_changeable(const struct option *poption);
struct option *option_next(const struct option *poption);

bool option_reset(struct option *poption);
void option_set_changed_callback(struct option *poption,
                                 void (*callback) (struct option *));
void option_changed(struct option *poption);

/* Option gui functions. */
void option_set_gui_data(struct option *poption, void *data);
void *option_get_gui_data(const struct option *poption);

/* Option type OT_BOOLEAN functions. */
bool option_bool_get(const struct option *poption);
bool option_bool_def(const struct option *poption);
bool option_bool_set(struct option *poption, bool val);

/* Option type OT_INTEGER functions. */
int option_int_get(const struct option *poption);
int option_int_def(const struct option *poption);
int option_int_min(const struct option *poption);
int option_int_max(const struct option *poption);
bool option_int_set(struct option *poption, int val);

/* Option type OT_STRING functions. */
const char *option_str_get(const struct option *poption);
const char *option_str_def(const struct option *poption);
const struct strvec *option_str_values(const struct option *poption);
bool option_str_set(struct option *poption, const char *str);

/* Option type OT_ENUM functions. */
int option_enum_str_to_int(const struct option *poption, const char *str);
const char *option_enum_int_to_str(const struct option *poption, int val);
int option_enum_get_int(const struct option *poption);
const char *option_enum_get_str(const struct option *poption);
int option_enum_def_int(const struct option *poption);
const char *option_enum_def_str(const struct option *poption);
const struct strvec *option_enum_values(const struct option *poption);
bool option_enum_set_int(struct option *poption, int val);
bool option_enum_set_str(struct option *poption, const char *str);

/* Option type OT_BITWISE functions. */
unsigned option_bitwise_get(const struct option *poption);
unsigned option_bitwise_def(const struct option *poption);
unsigned option_bitwise_mask(const struct option *poption);
const struct strvec *option_bitwise_values(const struct option *poption);
bool option_bitwise_set(struct option *poption, unsigned val);

/* Option type OT_FONT functions. */
const char *option_font_get(const struct option *poption);
const char *option_font_def(const struct option *poption);
const char *option_font_target(const struct option *poption);
bool option_font_set(struct option *poption, const char *font);

/* Option type OT_COLOR functions. */
struct ft_color option_color_get(const struct option *poption);
struct ft_color option_color_def(const struct option *poption);
bool option_color_set(struct option *poption, struct ft_color color);

/* Option type OT_VIDEO_MODE functions. */
struct video_mode option_video_mode_get(const struct option *poption);
struct video_mode option_video_mode_def(const struct option *poption);
bool option_video_mode_set(struct option *poption, struct video_mode mode);


#define options_iterate(poptset, poption)                                   \
{                                                                           \
  struct option *poption = optset_option_first(poptset);                    \
  for (; NULL != poption; poption = option_next(poption))                {  \

#define options_iterate_end                                                 \
  }                                                                         \
}


/** Desired settable options. **/
void desired_settable_options_update(void);
void desired_settable_option_update(const char *op_name,
                                    const char *op_value,
                                    bool allow_replace);


/** Dialog report options. **/
void options_dialogs_update(void);
void options_dialogs_set(void);


/** Message Options: **/

/* for specifying which event messages go where: */
#define NUM_MW 3
#define MW_OUTPUT    1		/* add to the output window */
#define MW_MESSAGES  2		/* add to the messages window */
#define MW_POPUP     4		/* popup an individual window */

extern int messages_where[];	/* OR-ed MW_ values [E_COUNT] */


/** Client options **/

#define GUI_DEFAULT_CHAT_LOGFILE        "freeciv-chat.log"

/* gui-gtk2: [xy]size of the city dialog */
#define GUI_GTK2_CITYDLG_DEFAULT_XSIZE  770
#define GUI_GTK2_CITYDLG_MIN_XSIZE      256
#define GUI_GTK2_CITYDLG_MAX_XSIZE      4096

#define GUI_GTK2_CITYDLG_DEFAULT_YSIZE  512
#define GUI_GTK2_CITYDLG_MIN_YSIZE      128
#define GUI_GTK2_CITYDLG_MAX_YSIZE      4096

#define GUI_GTK_OVERVIEW_MIN_XSIZE      160
#define GUI_GTK_OVERVIEW_MIN_YSIZE      100

/* gui-gtk3: [xy]size of the city dialog */
#define GUI_GTK3_CITYDLG_DEFAULT_XSIZE  770
#define GUI_GTK3_CITYDLG_MIN_XSIZE      256
#define GUI_GTK3_CITYDLG_MAX_XSIZE      4096

#define GUI_GTK3_CITYDLG_DEFAULT_YSIZE  512
#define GUI_GTK3_CITYDLG_MIN_YSIZE      128
#define GUI_GTK3_CITYDLG_MAX_YSIZE      4096

#define GUI_DEFAULT_MAPIMG_FILENAME     "freeciv"

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__OPTIONS_H */
