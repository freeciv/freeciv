/***********************************************************************
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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdlib.h>

#include <gtk/gtk.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "shared.h"
#include "support.h"

/* common */
#include "game.h"
#include "government.h"
#include "road.h"
#include "specialist.h"
#include "unit.h"

/* client */
#include "client_main.h"
#include "clinet.h"
#include "connectdlg_common.h"
#include "control.h"
#include "mapview_common.h"
#include "options.h"
#include "tilespec.h"

/* client/gui-gtk-3.22 */
#include "chatline.h"
#include "cityrep.h"
#include "dialogs.h"
#include "editgui.h"
#include "editprop.h"
#include "finddlg.h"
#include "gamedlgs.h"
#include "gotodlg.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "infradlg.h"
#include "luaconsole.h"
#include "mapctrl.h"            /* center_on_unit(). */
#include "messagedlg.h"
#include "messagewin.h"
#include "optiondlg.h"
#include "pages.h"
#include "plrdlg.h"
#include "rallypointdlg.h"
#include "ratesdlg.h"
#include "repodlgs.h"
#include "sprite.h"
#include "spaceshipdlg.h"
#include "unitselect.h"
#include "wldlg.h"

#include "menu.h"

#ifndef GTK_STOCK_EDIT
#define GTK_STOCK_EDIT NULL
#endif

static GtkBuilder *ui_builder = NULL;

static void menu_entry_set_active(const char *key,
                                  gboolean is_active);
static void menu_entry_set_sensitive(const char *key,
                                     gboolean is_sensitive);
#ifndef FREECIV_DEBUG
static void menu_entry_set_visible(const char *key,
                                   gboolean is_visible,
                                   gboolean is_sensitive);
#endif /* FREECIV_DEBUG */

static void view_menu_update_sensitivity(void);

enum menu_entry_grouping { MGROUP_SAFE, MGROUP_EDIT, MGROUP_PLAYING,
                           MGROUP_UNIT, MGROUP_PLAYER, MGROUP_ALL };

static void menu_entry_group_set_sensitive(enum menu_entry_grouping group,
                                           gboolean is_sensitive);

struct menu_entry_info {
  const char *key;
  const char *name;
  guint accel;
  GdkModifierType accel_mod;
  GCallback cb;
  enum menu_entry_grouping grouping;
};

static void clear_chat_logs_callback(GtkMenuItem *item, gpointer data);
static void save_chat_logs_callback(GtkMenuItem *item, gpointer data);
static void local_options_callback(GtkMenuItem *item, gpointer data);
static void message_options_callback(GtkMenuItem *item, gpointer data);
static void server_options_callback(GtkMenuItem *item, gpointer data);
static void save_options_callback(GtkMenuItem *item, gpointer data);
static void reload_tileset_callback(GtkMenuItem *item, gpointer data);
static void save_game_callback(GtkMenuItem *item, gpointer data);
static void save_game_as_callback(GtkMenuItem *item, gpointer data);
static void save_mapimg_callback(GtkMenuItem *item, gpointer data);
static void save_mapimg_as_callback(GtkMenuItem *item, gpointer data);
static void find_city_callback(GtkMenuItem *item, gpointer data);
static void worklists_callback(GtkMenuItem *item, gpointer data);
static void client_lua_script_callback(GtkMenuItem *item, gpointer data);
static void leave_callback(GtkMenuItem *item, gpointer data);
static void quit_callback(GtkMenuItem *item, gpointer data);
static void map_view_callback(GtkMenuItem *item, gpointer data);
static void report_units_callback(GtkMenuItem *item, gpointer data);
static void report_nations_callback(GtkMenuItem *item, gpointer data);
static void report_cities_callback(GtkMenuItem *item, gpointer data);
static void report_wow_callback(GtkMenuItem *item, gpointer data);
static void report_top_cities_callback(GtkMenuItem *item, gpointer data);
static void report_messages_callback(GtkMenuItem *item, gpointer data);
static void report_demographic_callback(GtkMenuItem *item, gpointer data);
static void bg_select0_callback(GtkMenuItem *item, gpointer data);
static void bg_assign0_callback(GtkMenuItem *item, gpointer data);
static void bg_append0_callback(GtkMenuItem *item, gpointer data);
static void bg_select1_callback(GtkMenuItem *item, gpointer data);
static void bg_assign1_callback(GtkMenuItem *item, gpointer data);
static void bg_append1_callback(GtkMenuItem *item, gpointer data);
static void bg_select2_callback(GtkMenuItem *item, gpointer data);
static void bg_assign2_callback(GtkMenuItem *item, gpointer data);
static void bg_append2_callback(GtkMenuItem *item, gpointer data);
static void bg_select3_callback(GtkMenuItem *item, gpointer data);
static void bg_assign3_callback(GtkMenuItem *item, gpointer data);
static void bg_append3_callback(GtkMenuItem *item, gpointer data);
static void help_overview_callback(GtkMenuItem *item, gpointer data);
static void help_playing_callback(GtkMenuItem *item, gpointer data);
static void help_policies_callback(GtkMenuItem *item, gpointer data);
static void help_terrain_callback(GtkMenuItem *item, gpointer data);
static void help_economy_callback(GtkMenuItem *item, gpointer data);
static void help_cities_callback(GtkMenuItem *item, gpointer data);
static void help_improvements_callback(GtkMenuItem *item, gpointer data);
static void help_wonders_callback(GtkMenuItem *item, gpointer data);
static void help_units_callback(GtkMenuItem *item, gpointer data);
static void help_combat_callback(GtkMenuItem *item, gpointer data);
static void help_zoc_callback(GtkMenuItem *item, gpointer data);
static void help_government_callback(GtkMenuItem *item, gpointer data);
static void help_diplomacy_callback(GtkMenuItem *item, gpointer data);
static void help_tech_callback(GtkMenuItem *item, gpointer data);
static void help_space_race_callback(GtkMenuItem *item, gpointer data);
static void help_ruleset_callback(GtkMenuItem *item, gpointer data);
static void help_tileset_callback(GtkMenuItem *item, gpointer data);
static void help_musicset_callback(GtkMenuItem *item, gpointer data);
static void help_nations_callback(GtkMenuItem *item, gpointer data);
static void help_connecting_callback(GtkMenuItem *item, gpointer data);
static void help_controls_callback(GtkMenuItem *item, gpointer data);
static void help_cma_callback(GtkMenuItem *item, gpointer data);
static void help_chatline_callback(GtkMenuItem *item, gpointer data);
static void help_worklist_editor_callback(GtkMenuItem *item, gpointer data);
static void help_language_callback(GtkMenuItem *item, gpointer data);
static void help_copying_callback(GtkMenuItem *item, gpointer data);
static void help_about_callback(GtkMenuItem *item, gpointer data);
static void save_options_on_exit_callback(GtkCheckMenuItem *item,
                                          gpointer data);
static void edit_mode_callback(GtkCheckMenuItem *item, gpointer data);
static void show_city_outlines_callback(GtkCheckMenuItem *item,
                                        gpointer data);
static void show_city_output_callback(GtkCheckMenuItem *item, gpointer data);
static void show_map_grid_callback(GtkCheckMenuItem *item, gpointer data);
static void show_national_borders_callback(GtkCheckMenuItem *item,
                                           gpointer data);
static void show_native_tiles_callback(GtkCheckMenuItem *item,
                                       gpointer data);
static void show_city_full_bar_callback(GtkCheckMenuItem *item,
                                        gpointer data);
static void show_city_names_callback(GtkCheckMenuItem *item, gpointer data);
static void show_city_growth_callback(GtkCheckMenuItem *item, gpointer data);
static void show_city_productions_callback(GtkCheckMenuItem *item,
                                           gpointer data);
static void show_city_buy_cost_callback(GtkCheckMenuItem *item,
                                        gpointer data);
static void show_city_trade_routes_callback(GtkCheckMenuItem *item,
                                            gpointer data);
static void show_terrain_callback(GtkCheckMenuItem *item, gpointer data);
static void show_coastline_callback(GtkCheckMenuItem *item, gpointer data);
static void show_paths_callback(GtkCheckMenuItem *item, gpointer data);
static void show_irrigation_callback(GtkCheckMenuItem *item, gpointer data);
static void show_mine_callback(GtkCheckMenuItem *item, gpointer data);
static void show_bases_callback(GtkCheckMenuItem *item, gpointer data);
static void show_resources_callback(GtkCheckMenuItem *item, gpointer data);
static void show_huts_callback(GtkCheckMenuItem *item, gpointer data);
static void show_pollution_callback(GtkCheckMenuItem *item, gpointer data);
static void show_cities_callback(GtkCheckMenuItem *item, gpointer data);
static void show_units_callback(GtkCheckMenuItem *item, gpointer data);
static void show_unit_solid_bg_callback(GtkCheckMenuItem *item,
                                        gpointer data);
static void show_unit_shields_callback(GtkCheckMenuItem *item,
                                       gpointer data);
static void show_stack_size_callback(GtkCheckMenuItem *item,
                                     gpointer data);
static void show_focus_unit_callback(GtkCheckMenuItem *item, gpointer data);
static void show_fog_of_war_callback(GtkCheckMenuItem *item, gpointer data);
static void full_screen_callback(GtkCheckMenuItem *item, gpointer data);
static void recalc_borders_callback(GtkMenuItem *item, gpointer data);
static void toggle_fog_callback(GtkCheckMenuItem *item, gpointer data);
static void scenario_properties_callback(GtkMenuItem *item, gpointer data);
static void save_scenario_callback(GtkMenuItem *item, gpointer data);
static void center_view_callback(GtkMenuItem *item, gpointer data);
static void report_economy_callback(GtkMenuItem *item, gpointer data);
static void report_research_callback(GtkMenuItem *item, gpointer data);
static void multiplier_callback(GtkMenuItem *item, gpointer data);
static void report_spaceship_callback(GtkMenuItem *item, gpointer data);
static void report_achievements_callback(GtkMenuItem *item, gpointer data);
static void government_callback(GtkMenuItem *item, gpointer data);
static void tax_rate_callback(GtkMenuItem *item, gpointer data);
static void select_single_callback(GtkMenuItem *item, gpointer data);
static void select_all_on_tile_callback(GtkMenuItem *item, gpointer data);
static void select_same_type_tile_callback(GtkMenuItem *item, gpointer data);
static void select_same_type_cont_callback(GtkMenuItem *item, gpointer data);
static void select_same_type_callback(GtkMenuItem *item, gpointer data);
static void select_dialog_callback(GtkMenuItem *item, gpointer data);
static void rally_dialog_callback(GtkMenuItem *item, gpointer data);
static void infra_dialog_callback(GtkMenuItem *item, gpointer data);
static void unit_wait_callback(GtkMenuItem *item, gpointer data);
static void unit_done_callback(GtkMenuItem *item, gpointer data);
static void unit_goto_callback(GtkMenuItem *item, gpointer data);
static void unit_goto_city_callback(GtkMenuItem *item, gpointer data);
static void unit_return_callback(GtkMenuItem *item, gpointer data);
static void unit_explore_callback(GtkMenuItem *item, gpointer data);
static void unit_patrol_callback(GtkMenuItem *item, gpointer data);
static void unit_sentry_callback(GtkMenuItem *item, gpointer data);
static void unit_unsentry_callback(GtkMenuItem *item, gpointer data);
static void unit_board_callback(GtkMenuItem *item, gpointer data);
static void unit_deboard_callback(GtkMenuItem *item, gpointer data);
static void unit_unload_transporter_callback(GtkMenuItem *item,
                                             gpointer data);
static void unit_homecity_callback(GtkMenuItem *item, gpointer data);
static void unit_upgrade_callback(GtkMenuItem *item, gpointer data);
static void unit_convert_callback(GtkMenuItem *item, gpointer data);
static void unit_disband_callback(GtkMenuItem *item, gpointer data);
static void build_city_callback(GtkMenuItem *item, gpointer data);
static void auto_work_callback(GtkMenuItem *item, gpointer data);
static void build_road_callback(GtkMenuItem *item, gpointer data);
static void build_irrigation_callback(GtkMenuItem *item, gpointer data);
static void cultivate_callback(GtkMenuItem *item, gpointer data);
static void build_mine_callback(GtkMenuItem *item, gpointer data);
static void plant_callback(GtkMenuItem *item, gpointer data);
static void connect_road_callback(GtkMenuItem *item, gpointer data);
static void connect_rail_callback(GtkMenuItem *item, gpointer data);
static void connect_maglev_callback(GtkMenuItem *item, gpointer data);
static void connect_irrigation_callback(GtkMenuItem *item, gpointer data);
static void transform_terrain_callback(GtkMenuItem *item, gpointer data);
static void clean_nuisance_callback(GtkMenuItem *item, gpointer data);
static void fortify_callback(GtkMenuItem *item, gpointer data);
static void build_fortress_callback(GtkMenuItem *item, gpointer data);
static void build_airbase_callback(GtkMenuItem *item, gpointer data);
static void do_paradrop_callback(GtkMenuItem *item, gpointer data);
static void do_pillage_callback(GtkMenuItem *item, gpointer data);
static void diplomat_action_callback(GtkMenuItem *item, gpointer data);

static struct menu_entry_info menu_entries[] =
{
  { "MENU_GAME", N_("_Game"), 0, 0, NULL, MGROUP_SAFE },
  { "MENU_OPTIONS", N_("_Options"), 0, 0, NULL, MGROUP_SAFE },
  { "MENU_EDIT", N_("_Edit"), 0, 0, NULL, MGROUP_SAFE },
  { "MENU_VIEW", N_("?verb:_View"), 0, 0, NULL, MGROUP_SAFE },
  { "MENU_IMPROVEMENTS", N_("_Improvements"), 0, 0,
    NULL, MGROUP_SAFE },
  { "MENU_CIVILIZATION", N_("C_ivilization"), 0, 0,
    NULL, MGROUP_SAFE },
  { "MENU_BATTLE_GROUPS", N_("_Battle Groups"), 0, 0,
    NULL, MGROUP_PLAYING },
  { "MENU_HELP", N_("_Help"), 0, 0, NULL, MGROUP_SAFE },
  { "CLEAR_CHAT_LOGS", N_("_Clear Chat Log"), 0, 0,
    G_CALLBACK(clear_chat_logs_callback), MGROUP_SAFE },
  { "SAVE_CHAT_LOGS", N_("Save Chat _Log"), 0, 0,
    G_CALLBACK(save_chat_logs_callback), MGROUP_SAFE },
  { "LOCAL_OPTIONS", N_("_Local Client"), 0, 0,
    G_CALLBACK(local_options_callback), MGROUP_SAFE },
  { "MESSAGE_OPTIONS", N_("_Message"), 0, 0,
    G_CALLBACK(message_options_callback), MGROUP_SAFE },
  { "SERVER_OPTIONS", N_("_Remote Server"), 0, 0,
    G_CALLBACK(server_options_callback), MGROUP_SAFE },
  { "SAVE_OPTIONS", N_("Save Options _Now"), 0, 0,
    G_CALLBACK(save_options_callback), MGROUP_SAFE },
  { "RELOAD_TILESET", N_("_Reload Tileset"),
    GDK_KEY_r, GDK_MOD1_MASK | ACCL_MOD_KEY,
    G_CALLBACK(reload_tileset_callback), MGROUP_SAFE },
  { "GAME_SAVE", N_("_Save Game"), GDK_KEY_s, ACCL_MOD_KEY,
    G_CALLBACK(save_game_callback), MGROUP_SAFE },
  { "GAME_SAVE_AS", N_("Save Game _As..."), 0, 0,
    G_CALLBACK(save_game_as_callback), MGROUP_SAFE },
  { "MAPIMG_SAVE", N_("Save Map _Image"), 0, 0,
    G_CALLBACK(save_mapimg_callback), MGROUP_SAFE },
  { "MAPIMG_SAVE_AS", N_("Save _Map Image As..."), 0, 0,
    G_CALLBACK(save_mapimg_as_callback), MGROUP_SAFE },
  { "LEAVE", N_("_Leave"), 0, 0, G_CALLBACK(leave_callback), MGROUP_SAFE },
  { "QUIT", N_("_Quit"), GDK_KEY_q, ACCL_MOD_KEY,
    G_CALLBACK(quit_callback), MGROUP_SAFE },
  { "FIND_CITY", N_("_Find City"), GDK_KEY_f, ACCL_MOD_KEY,
    G_CALLBACK(find_city_callback), MGROUP_SAFE },
  { "WORKLISTS", N_("Work_lists"),
    GDK_KEY_l, ACCL_MOD_KEY | GDK_SHIFT_MASK,
    G_CALLBACK(worklists_callback), MGROUP_SAFE },
  { "RALLY_DLG", N_("Rally point dialog"),
    GDK_KEY_r, ACCL_MOD_KEY | GDK_SHIFT_MASK,
    G_CALLBACK(rally_dialog_callback), MGROUP_SAFE },
  { "INFRA_DLG", N_("Infra dialog"),
    GDK_KEY_f, ACCL_MOD_KEY | GDK_SHIFT_MASK,
    G_CALLBACK(infra_dialog_callback), MGROUP_SAFE },
  { "CLIENT_LUA_SCRIPT", N_("Client _Lua Script"), 0, 0,
    G_CALLBACK(client_lua_script_callback), MGROUP_SAFE },
  { "MAP_VIEW", N_("?noun:_View"), GDK_KEY_F1, 0,
    G_CALLBACK(map_view_callback), MGROUP_SAFE },
  { "REPORT_UNITS", N_("_Units"), GDK_KEY_F2, 0,
    G_CALLBACK(report_units_callback), MGROUP_SAFE },
  { "REPORT_NATIONS", N_("_Nations"), GDK_KEY_F3, 0,
    G_CALLBACK(report_nations_callback), MGROUP_SAFE },
  { "REPORT_CITIES", N_("_Cities"), GDK_KEY_F4, 0,
    G_CALLBACK(report_cities_callback), MGROUP_SAFE },
  { "REPORT_WOW", N_("_Wonders of the World"), GDK_KEY_F7, 0,
    G_CALLBACK(report_wow_callback), MGROUP_SAFE },
  { "REPORT_TOP_CITIES", N_("Top Cities"), GDK_KEY_F8, 0,
    G_CALLBACK(report_top_cities_callback), MGROUP_SAFE },
  { "REPORT_MESSAGES", N_("_Messages"), GDK_KEY_F9, 0,
    G_CALLBACK(report_messages_callback), MGROUP_SAFE },
  { "REPORT_DEMOGRAPHIC", N_("_Demographics"), GDK_KEY_F11, 0,
    G_CALLBACK(report_demographic_callback), MGROUP_SAFE },

  /* Battle Groups menu */
  /* Note that user view: 1 - 4, internal: 0 - 3 */
  { "BATTLE_GROUP_SELECT_0", N_("Select Battle Group 1"),
    GDK_KEY_F1, GDK_SHIFT_MASK,
    G_CALLBACK(bg_select0_callback), MGROUP_SAFE },
  { "BATTLE_GROUP_ASSIGN_0", N_("Assign Battle Group 1"),
    GDK_KEY_F1, ACCL_MOD_KEY,
    G_CALLBACK(bg_assign0_callback), MGROUP_SAFE },
  { "BATTLE_GROUP_APPEND_0", N_("Append to Battle Group 1"),
    GDK_KEY_F1, GDK_SHIFT_MASK | ACCL_MOD_KEY,
    G_CALLBACK(bg_append0_callback), MGROUP_SAFE },
  { "BATTLE_GROUP_SELECT_1", N_("Select Battle Group 2"),
    GDK_KEY_F2, GDK_SHIFT_MASK,
    G_CALLBACK(bg_select1_callback), MGROUP_SAFE },
  { "BATTLE_GROUP_ASSIGN_1", N_("Assign Battle Group 2"),
    GDK_KEY_F2, ACCL_MOD_KEY,
    G_CALLBACK(bg_assign1_callback), MGROUP_SAFE },
  { "BATTLE_GROUP_APPEND_1", N_("Append to Battle Group 2"),
    GDK_KEY_F2, GDK_SHIFT_MASK | ACCL_MOD_KEY,
    G_CALLBACK(bg_append1_callback), MGROUP_SAFE },
  { "BATTLE_GROUP_SELECT_2", N_("Select Battle Group 3"),
    GDK_KEY_F3, GDK_SHIFT_MASK,
    G_CALLBACK(bg_select2_callback), MGROUP_SAFE },
  { "BATTLE_GROUP_ASSIGN_2", N_("Assign Battle Group 3"),
    GDK_KEY_F3, ACCL_MOD_KEY,
    G_CALLBACK(bg_assign2_callback), MGROUP_SAFE },
  { "BATTLE_GROUP_APPEND_2", N_("Append to Battle Group 3"),
    GDK_KEY_F3, GDK_SHIFT_MASK | ACCL_MOD_KEY,
    G_CALLBACK(bg_append2_callback), MGROUP_SAFE },
  { "BATTLE_GROUP_SELECT_3", N_("Select Battle Group 4"),
    GDK_KEY_F4, GDK_SHIFT_MASK,
    G_CALLBACK(bg_select3_callback), MGROUP_SAFE },
  { "BATTLE_GROUP_ASSIGN_3", N_("Assign Battle Group 4"),
    GDK_KEY_F4, ACCL_MOD_KEY,
    G_CALLBACK(bg_assign3_callback), MGROUP_SAFE },
  { "BATTLE_GROUP_APPEND_3", N_("Append to Battle Group 4"),
    GDK_KEY_F4, GDK_SHIFT_MASK | ACCL_MOD_KEY,
    G_CALLBACK(bg_append3_callback), MGROUP_SAFE },

  { "HELP_OVERVIEW", N_("?help:Overview"), 0, 0,
    G_CALLBACK(help_overview_callback), MGROUP_SAFE },
  { "HELP_PLAYING", N_("Strategy and Tactics"), 0, 0,
    G_CALLBACK(help_playing_callback), MGROUP_SAFE },
  { "HELP_POLICIES", N_("Policies"), 0, 0,
    G_CALLBACK(help_policies_callback), MGROUP_SAFE },
  { "HELP_TERRAIN", N_("Terrain"), 0, 0,
    G_CALLBACK(help_terrain_callback), MGROUP_SAFE },
  { "HELP_ECONOMY", N_("Economy"), 0, 0,
    G_CALLBACK(help_economy_callback), MGROUP_SAFE },
  { "HELP_CITIES", N_("Cities"), 0, 0,
    G_CALLBACK(help_cities_callback), MGROUP_SAFE },
  { "HELP_IMPROVEMENTS", N_("City Improvements"), 0, 0,
    G_CALLBACK(help_improvements_callback), MGROUP_SAFE },
  { "HELP_WONDERS", N_("Wonders of the World"), 0, 0,
    G_CALLBACK(help_wonders_callback), MGROUP_SAFE },
  { "HELP_UNITS", N_("Units"), 0, 0,
    G_CALLBACK(help_units_callback), MGROUP_SAFE },
  { "HELP_COMBAT", N_("Combat"), 0, 0,
    G_CALLBACK(help_combat_callback), MGROUP_SAFE },
  { "HELP_ZOC", N_("Zones of Control"), 0, 0,
    G_CALLBACK(help_zoc_callback), MGROUP_SAFE },
  { "HELP_GOVERNMENT", N_("Government"), 0, 0,
    G_CALLBACK(help_government_callback), MGROUP_SAFE },
  { "HELP_DIPLOMACY", N_("Diplomacy"), 0, 0,
    G_CALLBACK(help_diplomacy_callback), MGROUP_SAFE },
  { "HELP_TECH", N_("Technology"), 0, 0,
    G_CALLBACK(help_tech_callback), MGROUP_SAFE },
  { "HELP_SPACE_RACE", N_("Space Race"), 0, 0,
    G_CALLBACK(help_space_race_callback), MGROUP_SAFE },
  { "HELP_RULESET", N_("About Current Ruleset"), 0, 0,
    G_CALLBACK(help_ruleset_callback), MGROUP_SAFE },
  { "HELP_TILESET", N_("About Current Tileset"), 0, 0,
    G_CALLBACK(help_tileset_callback), MGROUP_SAFE },
  { "HELP_MUSICSET", N_("About Current Musicset"), 0, 0,
    G_CALLBACK(help_musicset_callback), MGROUP_SAFE },
  { "HELP_NATIONS", N_("About Nations"), 0, 0,
    G_CALLBACK(help_nations_callback), MGROUP_SAFE },
  { "HELP_CONNECTING", N_("Connecting"), 0, 0,
    G_CALLBACK(help_connecting_callback), MGROUP_SAFE },
  { "HELP_CONTROLS", N_("Controls"), 0, 0,
    G_CALLBACK(help_controls_callback), MGROUP_SAFE },
  { "HELP_CMA", N_("Citizen Governor"), 0, 0,
    G_CALLBACK(help_cma_callback), MGROUP_SAFE },
  { "HELP_CHATLINE", N_("Chatline"), 0, 0,
    G_CALLBACK(help_chatline_callback), MGROUP_SAFE },
  { "HELP_WORKLIST_EDITOR", N_("Worklist Editor"), 0, 0,
    G_CALLBACK(help_worklist_editor_callback), MGROUP_SAFE },
  { "HELP_LANGUAGES", N_("Languages"), 0, 0,
    G_CALLBACK(help_language_callback), MGROUP_SAFE },
  { "HELP_COPYING", N_("Copying"), 0, 0,
    G_CALLBACK(help_copying_callback), MGROUP_SAFE },
  { "HELP_ABOUT", N_("About Freeciv"), 0, 0,
    G_CALLBACK(help_about_callback), MGROUP_SAFE },
  { "SAVE_OPTIONS_ON_EXIT", N_("Save Options on _Exit"), 0, 0,
    G_CALLBACK(save_options_on_exit_callback), MGROUP_SAFE },
  { "EDIT_MODE", N_("_Editing Mode"), GDK_KEY_e, ACCL_MOD_KEY,
    G_CALLBACK(edit_mode_callback), MGROUP_SAFE },
  { "SHOW_CITY_OUTLINES", N_("Cit_y Outlines"), GDK_KEY_y, ACCL_MOD_KEY,
    G_CALLBACK(show_city_outlines_callback), MGROUP_SAFE },
  { "SHOW_CITY_OUTPUT", N_("City Output"), GDK_KEY_v, ACCL_MOD_KEY,
    G_CALLBACK(show_city_output_callback), MGROUP_SAFE },
  { "SHOW_MAP_GRID", N_("Map _Grid"), GDK_KEY_g, ACCL_MOD_KEY,
    G_CALLBACK(show_map_grid_callback), MGROUP_SAFE },
  { "SHOW_NATIONAL_BORDERS", N_("National _Borders"), GDK_KEY_b, ACCL_MOD_KEY,
    G_CALLBACK(show_national_borders_callback), MGROUP_SAFE },
  { "SHOW_NATIVE_TILES", N_("Native Tiles"), GDK_KEY_n, ACCL_MOD_KEY | GDK_SHIFT_MASK,
    G_CALLBACK(show_native_tiles_callback), MGROUP_SAFE },
  { "SHOW_CITY_FULL_BAR", N_("City Full Bar"), 0, 0,
    G_CALLBACK(show_city_full_bar_callback), MGROUP_SAFE },
  { "SHOW_CITY_NAMES", N_("City _Names"), GDK_KEY_n, ACCL_MOD_KEY,
    G_CALLBACK(show_city_names_callback), MGROUP_SAFE },
  { "SHOW_CITY_GROWTH", N_("City G_rowth"), GDK_KEY_o, ACCL_MOD_KEY,
    G_CALLBACK(show_city_growth_callback), MGROUP_SAFE },
  { "SHOW_CITY_PRODUCTIONS", N_("City _Production"), GDK_KEY_p, ACCL_MOD_KEY,
    G_CALLBACK(show_city_productions_callback), MGROUP_SAFE },
  { "SHOW_CITY_BUY_COST", N_("City Buy Cost"), 0, 0,
    G_CALLBACK(show_city_buy_cost_callback), MGROUP_SAFE },
  { "SHOW_CITY_TRADE_ROUTES", N_("City Tra_deroutes"), GDK_KEY_d, ACCL_MOD_KEY,
    G_CALLBACK(show_city_trade_routes_callback), MGROUP_SAFE },
  { "SHOW_TERRAIN", N_("_Terrain"), 0, 0,
    G_CALLBACK(show_terrain_callback), MGROUP_SAFE },
  { "SHOW_COASTLINE", N_("C_oastline"), 0, 0,
    G_CALLBACK(show_coastline_callback), MGROUP_SAFE },
  { "SHOW_PATHS", N_("_Paths"), 0, 0,
    G_CALLBACK(show_paths_callback), MGROUP_SAFE },
  { "SHOW_IRRIGATION", N_("_Irrigation"), 0, 0,
    G_CALLBACK(show_irrigation_callback), MGROUP_SAFE },
  { "SHOW_MINES", N_("_Mines"), 0, 0,
    G_CALLBACK(show_mine_callback), MGROUP_SAFE },
  { "SHOW_BASES", N_("_Bases"), 0, 0,
    G_CALLBACK(show_bases_callback), MGROUP_SAFE },
  { "SHOW_RESOURCES", N_("_Resources"), 0, 0,
    G_CALLBACK(show_resources_callback), MGROUP_SAFE },
  { "SHOW_HUTS", N_("_Huts"), 0, 0,
    G_CALLBACK(show_huts_callback), MGROUP_SAFE },
  { "SHOW_POLLUTION", N_("Po_llution & Fallout"), 0, 0,
    G_CALLBACK(show_pollution_callback), MGROUP_SAFE },
  { "SHOW_CITIES", N_("Citi_es"), 0, 0,
    G_CALLBACK(show_cities_callback), MGROUP_SAFE },
  { "SHOW_UNITS", N_("_Units"), 0, 0,
    G_CALLBACK(show_units_callback), MGROUP_SAFE },
  { "SHOW_UNIT_SOLID_BG", N_("Unit Solid Background"), 0, 0,
    G_CALLBACK(show_unit_solid_bg_callback), MGROUP_SAFE },
  { "SHOW_UNIT_SHIELDS", N_("Unit shields"), 0, 0,
    G_CALLBACK(show_unit_shields_callback), MGROUP_SAFE },
  { "SHOW_STACK_SIZE", N_("Unit Stack Size"),
    GDK_KEY_plus, ACCL_MOD_KEY,
    G_CALLBACK(show_stack_size_callback), MGROUP_SAFE },
  { "SHOW_FOCUS_UNIT", N_("Focu_s Unit"), 0, 0,
    G_CALLBACK(show_focus_unit_callback), MGROUP_SAFE },
  { "SHOW_FOG_OF_WAR", N_("Fog of _War"), 0, 0,
    G_CALLBACK(show_fog_of_war_callback), MGROUP_SAFE },
  { "FULL_SCREEN", N_("_Fullscreen"), GDK_KEY_Return, GDK_MOD1_MASK,
    G_CALLBACK(full_screen_callback), MGROUP_SAFE },

  { "RECALC_BORDERS", N_("Recalculate _Borders"), 0, 0,
    G_CALLBACK(recalc_borders_callback), MGROUP_EDIT },
  { "TOGGLE_FOG", N_("Toggle Fog of _War"), GDK_KEY_w,
    ACCL_MOD_KEY | GDK_SHIFT_MASK,
    G_CALLBACK(toggle_fog_callback), MGROUP_EDIT },
  { "SCENARIO_PROPERTIES", N_("Game/Scenario Properties"), 0, 0,
    G_CALLBACK(scenario_properties_callback), MGROUP_EDIT },
  { "SAVE_SCENARIO", N_("Save Scenario"), 0, 0,
    G_CALLBACK(save_scenario_callback), MGROUP_EDIT },

  { "CENTER_VIEW", N_("_Center View"), GDK_KEY_c, 0,
    G_CALLBACK(center_view_callback), MGROUP_PLAYER },
  { "REPORT_ECONOMY", N_("_Economy"), GDK_KEY_F5, 0,
    G_CALLBACK(report_economy_callback), MGROUP_PLAYER },
  { "REPORT_RESEARCH", N_("_Research"), GDK_KEY_F6, 0,
    G_CALLBACK(report_research_callback), MGROUP_PLAYER },
  { "POLICIES", N_("_Policies..."),
    GDK_KEY_p, GDK_SHIFT_MASK | ACCL_MOD_KEY,
    G_CALLBACK(multiplier_callback), MGROUP_PLAYER },
  { "REPORT_SPACESHIP", N_("_Spaceship"), GDK_KEY_F12, 0,
    G_CALLBACK(report_spaceship_callback), MGROUP_PLAYER },
  { "REPORT_ACHIEVEMENTS", N_("_Achievements"), GDK_KEY_asterisk, 0,
    G_CALLBACK(report_achievements_callback), MGROUP_PLAYER },

  { "MENU_SELECT", N_("_Select"), 0, 0, NULL, MGROUP_UNIT },
  { "MENU_UNIT", N_("_Unit"), 0, 0, NULL, MGROUP_UNIT },
  { "MENU_WORK", N_("_Work"), 0, 0, NULL, MGROUP_UNIT },
  { "MENU_COMBAT", N_("_Combat"), 0, 0, NULL, MGROUP_UNIT },
  { "MENU_BUILD_BASE", N_("Build _Base"), 0, 0, NULL, MGROUP_UNIT },
  { "MENU_BUILD_PATH", N_("Build _Path"), 0, 0, NULL, MGROUP_UNIT },
  { "MENU_CLEAN", N_("_Clean Nuisance"), 0, 0, NULL, MGROUP_UNIT },
  { "SELECT_SINGLE", N_("_Single Unit (Unselect Others)"), GDK_KEY_z, 0,
    G_CALLBACK(select_single_callback), MGROUP_UNIT },
  { "SELECT_ALL_ON_TILE", N_("_All On Tile"), GDK_KEY_v, 0,
    G_CALLBACK(select_all_on_tile_callback), MGROUP_UNIT },
  { "SELECT_SAME_TYPE_TILE", N_("Same Type on _Tile"),
    GDK_KEY_v, GDK_SHIFT_MASK,
    G_CALLBACK(select_same_type_tile_callback), MGROUP_UNIT },
  { "SELECT_SAME_TYPE_CONT", N_("Same Type on _Continent"),
    GDK_KEY_c, GDK_SHIFT_MASK,
    G_CALLBACK(select_same_type_cont_callback), MGROUP_UNIT },
  { "SELECT_SAME_TYPE", N_("Same Type _Everywhere"), GDK_KEY_x, GDK_SHIFT_MASK,
    G_CALLBACK(select_same_type_callback), MGROUP_UNIT },
  { "SELECT_DLG", N_("Unit Selection Dialog"), 0, 0,
    G_CALLBACK(select_dialog_callback), MGROUP_UNIT },
  { "UNIT_WAIT", N_("_Wait"), GDK_KEY_w, 0,
    G_CALLBACK(unit_wait_callback), MGROUP_UNIT },
  { "UNIT_DONE", N_("_Done"), GDK_KEY_space, 0,
    G_CALLBACK(unit_done_callback), MGROUP_UNIT },
  { "UNIT_GOTO", N_("_Go to"), GDK_KEY_g, 0,
    G_CALLBACK(unit_goto_callback), MGROUP_UNIT },
  { "MENU_GOTO_AND", N_("Go to a_nd..."), 0, 0, NULL, MGROUP_UNIT },
  { "UNIT_GOTO_CITY", N_("Go _to/Airlift to City..."), GDK_KEY_t, 0,
    G_CALLBACK(unit_goto_city_callback), MGROUP_UNIT },
  { "UNIT_RETURN", N_("_Return to Nearest City"), GDK_KEY_g, GDK_SHIFT_MASK,
    G_CALLBACK(unit_return_callback), MGROUP_UNIT },
  { "UNIT_EXPLORE", N_("Auto E_xplore"), GDK_KEY_x, 0,
    G_CALLBACK(unit_explore_callback), MGROUP_UNIT },
  { "UNIT_PATROL", N_("_Patrol"), GDK_KEY_q, 0,
    G_CALLBACK(unit_patrol_callback), MGROUP_UNIT },
  { "UNIT_SENTRY", N_("_Sentry"), GDK_KEY_s, 0,
    G_CALLBACK(unit_sentry_callback), MGROUP_UNIT },
  { "UNIT_UNSENTRY", N_("Uns_entry All On Tile"), GDK_KEY_s, GDK_SHIFT_MASK,
    G_CALLBACK(unit_unsentry_callback), MGROUP_UNIT },
  { "UNIT_BOARD", N_("_Load"), GDK_KEY_l, 0,
    G_CALLBACK(unit_board_callback), MGROUP_UNIT },
  { "UNIT_DEBOARD", N_("_Unload"), GDK_KEY_u, 0,
    G_CALLBACK(unit_deboard_callback), MGROUP_UNIT },
  { "UNIT_UNLOAD_TRANSPORTER", N_("U_nload All From Transporter"),
    GDK_KEY_t, GDK_SHIFT_MASK,
    G_CALLBACK(unit_unload_transporter_callback), MGROUP_UNIT },
  { "UNIT_HOMECITY", N_("Set _Home City"), GDK_KEY_h, 0,
    G_CALLBACK(unit_homecity_callback), MGROUP_UNIT },
  { "UNIT_UPGRADE", N_("Upgr_ade"), GDK_KEY_u, GDK_SHIFT_MASK,
    G_CALLBACK(unit_upgrade_callback), MGROUP_UNIT },
  { "UNIT_CONVERT", N_("C_onvert"), GDK_KEY_o, GDK_SHIFT_MASK,
    G_CALLBACK(unit_convert_callback), MGROUP_UNIT },
  { "UNIT_DISBAND", N_("_Disband"), GDK_KEY_d, GDK_SHIFT_MASK,
    G_CALLBACK(unit_disband_callback), MGROUP_UNIT },
  { "BUILD_CITY", N_("_Build City"), GDK_KEY_b, 0,
    G_CALLBACK(build_city_callback), MGROUP_UNIT },
  { "AUTO_WORKER", N_("_Auto Worker"), GDK_KEY_a, 0,
    G_CALLBACK(auto_work_callback), MGROUP_UNIT },
  { "BUILD_ROAD", N_("Build _Road"), GDK_KEY_r, 0,
    G_CALLBACK(build_road_callback), MGROUP_UNIT },
  { "BUILD_IRRIGATION", N_("Build _Irrigation"), GDK_KEY_i, 0,
    G_CALLBACK(build_irrigation_callback), MGROUP_UNIT },
  { "CULTIVATE", N_("Cultivate"), GDK_KEY_i, GDK_SHIFT_MASK,
    G_CALLBACK(cultivate_callback), MGROUP_UNIT },
  { "BUILD_MINE", N_("Build _Mine"), GDK_KEY_m, 0,
    G_CALLBACK(build_mine_callback), MGROUP_UNIT },
  { "PLANT", N_("Plant"), GDK_KEY_m, GDK_SHIFT_MASK,
    G_CALLBACK(plant_callback), MGROUP_UNIT },
  { "CONNECT_ROAD", N_("Connect With Roa_d"), GDK_KEY_r, ACCL_MOD_KEY,
    G_CALLBACK(connect_road_callback), MGROUP_UNIT },
  { "CONNECT_RAIL", N_("Connect With Rai_l"), GDK_KEY_l, ACCL_MOD_KEY,
    G_CALLBACK(connect_rail_callback), MGROUP_UNIT },
  { "CONNECT_MAGLEV", N_("Connect With _Maglev"), GDK_KEY_m, ACCL_MOD_KEY,
    G_CALLBACK(connect_maglev_callback), MGROUP_UNIT },
  { "CONNECT_IRRIGATION", N_("Connect With Irri_gation"),
    GDK_KEY_i, ACCL_MOD_KEY,
    G_CALLBACK(connect_irrigation_callback), MGROUP_UNIT },
  { "TRANSFORM_TERRAIN", N_("Transf_orm Terrain"), GDK_KEY_o, 0,
    G_CALLBACK(transform_terrain_callback), MGROUP_UNIT },
  { "CLEAN", N_("_Clean"), GDK_KEY_p, 0,
    G_CALLBACK(clean_nuisance_callback), MGROUP_UNIT },
  { "FORTIFY", N_("Fortify"), GDK_KEY_f, 0,
    G_CALLBACK(fortify_callback), MGROUP_UNIT },
  { "BUILD_FORTRESS", N_("Build Fortress"), GDK_KEY_f, GDK_SHIFT_MASK,
    G_CALLBACK(build_fortress_callback), MGROUP_UNIT },
  { "BUILD_AIRBASE", N_("Build Airbase"), GDK_KEY_e, GDK_SHIFT_MASK,
    G_CALLBACK(build_airbase_callback), MGROUP_UNIT },
  { "DO_PARADROP", N_("P_aradrop"), GDK_KEY_j, 0,
    G_CALLBACK(do_paradrop_callback), MGROUP_UNIT },
  { "DO_PILLAGE", N_("_Pillage"), GDK_KEY_p, GDK_SHIFT_MASK,
    G_CALLBACK(do_pillage_callback), MGROUP_UNIT },
  { "DIPLOMAT_ACTION", N_("_Do..."), GDK_KEY_d, 0,
    G_CALLBACK(diplomat_action_callback), MGROUP_UNIT },

  { "MENU_GOVERNMENT", N_("_Government"), 0, 0,
    NULL, MGROUP_PLAYING },
  { "TAX_RATE", N_("_Tax Rates..."), GDK_KEY_t, ACCL_MOD_KEY,
    G_CALLBACK(tax_rate_callback), MGROUP_PLAYING },
  { "START_REVOLUTION", N_("_Revolution..."),
    GDK_KEY_g, GDK_SHIFT_MASK | ACCL_MOD_KEY,
    G_CALLBACK(government_callback), MGROUP_PLAYING },
  { NULL }
};

static struct menu_entry_info *menu_entry_info_find(const char *key);

/************************************************************************//**
  Item "CLEAR_CHAT_LOGS" callback.
****************************************************************************/
static void clear_chat_logs_callback(GtkMenuItem *item, gpointer data)
{
  clear_output_window();
}

/************************************************************************//**
  Item "SAVE_CHAT_LOGS" callback.
****************************************************************************/
static void save_chat_logs_callback(GtkMenuItem *item, gpointer data)
{
  log_output_window();
}

/************************************************************************//**
  Item "LOCAL_OPTIONS" callback.
****************************************************************************/
static void local_options_callback(GtkMenuItem *item, gpointer data)
{
  option_dialog_popup(_("Set local options"), client_optset);
}

/************************************************************************//**
  Item "MESSAGE_OPTIONS" callback.
****************************************************************************/
static void message_options_callback(GtkMenuItem *item, gpointer data)
{
  popup_messageopt_dialog();
}

/************************************************************************//**
  Item "SERVER_OPTIONS" callback.
****************************************************************************/
static void server_options_callback(GtkMenuItem *item, gpointer data)
{
  option_dialog_popup(_("Game Settings"), server_optset);
}

/************************************************************************//**
  Item "SAVE_OPTIONS" callback.
****************************************************************************/
static void save_options_callback(GtkMenuItem *item, gpointer data)
{
  options_save(NULL);
}

/************************************************************************//**
  Item "RELOAD_TILESET" callback.
****************************************************************************/
static void reload_tileset_callback(GtkMenuItem *item, gpointer data)
{
  tilespec_reread(NULL, TRUE, 1.0);
}

/************************************************************************//**
  Item "SAVE_GAME" callback.
****************************************************************************/
static void save_game_callback(GtkMenuItem *item, gpointer data)
{
  send_save_game(NULL);
}

/************************************************************************//**
  Item "SAVE_GAME_AS" callback.
****************************************************************************/
static void save_game_as_callback(GtkMenuItem *item, gpointer data)
{
  save_game_dialog_popup();
}

/************************************************************************//**
  Item "SAVE_MAPIMG" callback.
****************************************************************************/
static void save_mapimg_callback(GtkMenuItem *item, gpointer data)
{
  mapimg_client_save(NULL);
}

/************************************************************************//**
  Item "SAVE_MAPIMG_AS" callback.
****************************************************************************/
static void save_mapimg_as_callback(GtkMenuItem *item, gpointer data)
{
  save_mapimg_dialog_popup();
}

/************************************************************************//**
  This is the response callback for the dialog with the message:
  Leaving a local game will end it!
****************************************************************************/
static void leave_local_game_response(GtkWidget *dialog, gint response)
{
  gtk_widget_destroy(dialog);
  if (response == GTK_RESPONSE_OK) {
    /* It might be killed already */
    if (client.conn.used) {
      /* It will also kill the server */
      disconnect_from_server(TRUE);
    }
  }
}

/************************************************************************//**
  Item "LEAVE" callback.
****************************************************************************/
static void leave_callback(GtkMenuItem *item, gpointer data)
{
  if (is_server_running()) {
    GtkWidget* dialog =
        gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_WARNING,
                               GTK_BUTTONS_OK_CANCEL,
                               _("Leaving a local game will end it!"));
    setup_dialog(dialog, toplevel);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(leave_local_game_response), NULL);
    gtk_window_present(GTK_WINDOW(dialog));
  } else {
    disconnect_from_server(TRUE);
  }
}

/************************************************************************//**
  Item "QUIT" callback.
****************************************************************************/
static void quit_callback(GtkMenuItem *item, gpointer data)
{
  popup_quit_dialog();
}

/************************************************************************//**
  Item "FIND_CITY" callback.
****************************************************************************/
static void find_city_callback(GtkMenuItem *item, gpointer data)
{
  popup_find_dialog();
}

/************************************************************************//**
  Item "WORKLISTS" callback.
****************************************************************************/
static void worklists_callback(GtkMenuItem *item, gpointer data)
{
  popup_worklists_report();
}

/************************************************************************//**
  Item "MAP_VIEW" callback.
****************************************************************************/
static void map_view_callback(GtkMenuItem *item, gpointer data)
{
  map_canvas_focus();
}

/************************************************************************//**
  Item "REPORT_NATIONS" callback.
****************************************************************************/
static void report_nations_callback(GtkMenuItem *item, gpointer data)
{
  popup_players_dialog(TRUE);
}

/************************************************************************//**
  Item "REPORT_WOW" callback.
****************************************************************************/
static void report_wow_callback(GtkMenuItem *item, gpointer data)
{
  if (GUI_GTK_OPTION(message_chat_location) == GUI_GTK_MSGCHAT_MERGED) {
    send_report_request(REPORT_WONDERS_OF_THE_WORLD_LONG);
  } else {
    send_report_request(REPORT_WONDERS_OF_THE_WORLD);
  }
}

/************************************************************************//**
  Item "REPORT_TOP_CITIES" callback.
****************************************************************************/
static void report_top_cities_callback(GtkMenuItem *item, gpointer data)
{
  send_report_request(REPORT_TOP_CITIES);
}

/************************************************************************//**
  Item "REPORT_MESSAGES" callback.
****************************************************************************/
static void report_messages_callback(GtkMenuItem *item, gpointer data)
{
  meswin_dialog_popup(TRUE);
}

/************************************************************************//**
  Item "CLIENT_LUA_SCRIPT" callback.
****************************************************************************/
static void client_lua_script_callback(GtkMenuItem *item, gpointer data)
{
  luaconsole_dialog_popup(TRUE);
}

/************************************************************************//**
  Item "REPORT_DEMOGRAPHIC" callback.
****************************************************************************/
static void report_demographic_callback(GtkMenuItem *item, gpointer data)
{
  send_report_request(REPORT_DEMOGRAPHIC);
}

/************************************************************************//**
  Item "REPORT_ACHIEVEMENTS" callback.
****************************************************************************/
static void report_achievements_callback(GtkMenuItem *item, gpointer data)
{
  send_report_request(REPORT_ACHIEVEMENTS);
}

/************************************************************************//**
  Item "HELP_LANGUAGE" callback.
****************************************************************************/
static void help_language_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_LANGUAGES_ITEM);
}

/************************************************************************//**
  Item "HELP_POLICIES" callback.
****************************************************************************/
static void help_policies_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_MULTIPLIER_ITEM);
}

/************************************************************************//**
  Item "HELP_CONNECTING" callback.
****************************************************************************/
static void help_connecting_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_CONNECTING_ITEM);
}

/************************************************************************//**
  Item "HELP_CONTROLS" callback.
****************************************************************************/
static void help_controls_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_CONTROLS_ITEM);
}

/************************************************************************//**
  Item "HELP_CHATLINE" callback.
****************************************************************************/
static void help_chatline_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_CHATLINE_ITEM);
}

/************************************************************************//**
  Item "HELP_WORKLIST_EDITOR" callback.
****************************************************************************/
static void help_worklist_editor_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_WORKLIST_EDITOR_ITEM);
}

/************************************************************************//**
  Item "HELP_CMA" callback.
****************************************************************************/
static void help_cma_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_CMA_ITEM);
}

/************************************************************************//**
  Item "BATTLE_GROUP_SELECT_0" callback.
****************************************************************************/
static void bg_select0_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_select_battlegroup(0, FALSE);
}

/************************************************************************//**
  Item "BATTLE_GROUP_ASSIGN_0" callback.
****************************************************************************/
static void bg_assign0_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_assign_battlegroup(0, FALSE);
}

/************************************************************************//**
  Item "BATTLE_GROUP_APPEND_0" callback.
****************************************************************************/
static void bg_append0_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_assign_battlegroup(0, TRUE);
}

/************************************************************************//**
  Item "BATTLE_GROUP_SELECT_1" callback.
****************************************************************************/
static void bg_select1_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_select_battlegroup(1, FALSE);
}

/************************************************************************//**
  Item "BATTLE_GROUP_ASSIGN_1" callback.
****************************************************************************/
static void bg_assign1_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_assign_battlegroup(1, FALSE);
}

/************************************************************************//**
  Item "BATTLE_GROUP_APPEND_1" callback.
****************************************************************************/
static void bg_append1_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_assign_battlegroup(1, TRUE);
}

/************************************************************************//**
  Item "BATTLE_GROUP_SELECT_2" callback.
****************************************************************************/
static void bg_select2_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_select_battlegroup(2, FALSE);
}

/************************************************************************//**
  Item "BATTLE_GROUP_ASSIGN_2" callback.
****************************************************************************/
static void bg_assign2_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_assign_battlegroup(2, FALSE);
}

/************************************************************************//**
  Item "BATTLE_GROUP_APPEND_2" callback.
****************************************************************************/
static void bg_append2_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_assign_battlegroup(2, TRUE);
}

/************************************************************************//**
  Item "BATTLE_GROUP_SELECT_3" callback.
****************************************************************************/
static void bg_select3_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_select_battlegroup(3, FALSE);
}

/************************************************************************//**
  Item "BATTLE_GROUP_ASSIGN_3" callback.
****************************************************************************/
static void bg_assign3_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_assign_battlegroup(3, FALSE);
}

/************************************************************************//**
  Item "BATTLE_GROUP_APPEND_3" callback.
****************************************************************************/
static void bg_append3_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_assign_battlegroup(3, TRUE);
}

/************************************************************************//**
  Item "HELP_OVERVIEW" callback.
****************************************************************************/
static void help_overview_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_OVERVIEW_ITEM);
}

/************************************************************************//**
  Item "HELP_PLAYING" callback.
****************************************************************************/
static void help_playing_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_PLAYING_ITEM);
}

/************************************************************************//**
  Item "HELP_RULESET" callback.
****************************************************************************/
static void help_ruleset_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_RULESET_ITEM);
}

/************************************************************************//**
  Item "HELP_TILESET" callback.
****************************************************************************/
static void help_tileset_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_TILESET_ITEM);
}

/************************************************************************//**
  Item "HELP_MUSICSET" callback.
****************************************************************************/
static void help_musicset_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_MUSICSET_ITEM);
}

/************************************************************************//**
  Item "HELP_ECONOMY" callback.
****************************************************************************/
static void help_economy_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_ECONOMY_ITEM);
}

/************************************************************************//**
  Item "HELP_CITIES" callback.
****************************************************************************/
static void help_cities_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_CITIES_ITEM);
}

/************************************************************************//**
  Item "HELP_IMPROVEMENTS" callback.
****************************************************************************/
static void help_improvements_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_IMPROVEMENTS_ITEM);
}

/************************************************************************//**
  Item "HELP_UNITS" callback.
****************************************************************************/
static void help_units_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_UNITS_ITEM);
}

/************************************************************************//**
  Item "HELP_COMBAT" callback.
****************************************************************************/
static void help_combat_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_COMBAT_ITEM);
}

/************************************************************************//**
  Item "HELP_ZOC" callback.
****************************************************************************/
static void help_zoc_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_ZOC_ITEM);
}

/************************************************************************//**
  Item "HELP_TECH" callback.
****************************************************************************/
static void help_tech_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_TECHS_ITEM);
}

/************************************************************************//**
  Item "HELP_TERRAIN" callback.
****************************************************************************/
static void help_terrain_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_TERRAIN_ITEM);
}

/************************************************************************//**
  Item "HELP_WONDERS" callback.
****************************************************************************/
static void help_wonders_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_WONDERS_ITEM);
}

/************************************************************************//**
  Item "HELP_GOVERNMENT" callback.
****************************************************************************/
static void help_government_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_GOVERNMENT_ITEM);
}

/************************************************************************//**
  Item "HELP_DIPLOMACY" callback.
****************************************************************************/
static void help_diplomacy_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_DIPLOMACY_ITEM);
}

/************************************************************************//**
  Item "HELP_SPACE_RACE" callback.
****************************************************************************/
static void help_space_race_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_SPACE_RACE_ITEM);
}

/************************************************************************//**
  Item "HELP_NATIONS" callback.
****************************************************************************/
static void help_nations_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_NATIONS_ITEM);
}

/************************************************************************//**
  Item "HELP_COPYING" callback.
****************************************************************************/
static void help_copying_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_COPYING_ITEM);
}

/************************************************************************//**
  Item "HELP_ABOUT" callback.
****************************************************************************/
static void help_about_callback(GtkMenuItem *item, gpointer data)
{
  popup_help_dialog_string(HELP_ABOUT_ITEM);
}

/************************************************************************//**
  Item "SAVE_OPTIONS_ON_EXIT" callback.
****************************************************************************/
static void save_options_on_exit_callback(GtkCheckMenuItem *item,
                                          gpointer data)
{
  gui_options.save_options_on_exit = gtk_check_menu_item_get_active(item);
}

/************************************************************************//**
  Item "EDIT_MODE" callback.
****************************************************************************/
static void edit_mode_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (game.info.is_edit_mode ^ gtk_check_menu_item_get_active(item)) {
    key_editor_toggle();
    /* Unreachable techs in reqtree on/off */
    science_report_dialog_popdown();
  }
}

/************************************************************************//**
  Item "SHOW_CITY_OUTLINES" callback.
****************************************************************************/
static void show_city_outlines_callback(GtkCheckMenuItem *item,
                                        gpointer data)
{
  if (gui_options.draw_city_outlines ^ gtk_check_menu_item_get_active(item)) {
    key_city_outlines_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_CITY_OUTPUT" callback.
****************************************************************************/
static void show_city_output_callback(GtkCheckMenuItem *item,
                                      gpointer data)
{
  if (gui_options.draw_city_output ^ gtk_check_menu_item_get_active(item)) {
    key_city_output_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_MAP_GRID" callback.
****************************************************************************/
static void show_map_grid_callback(GtkCheckMenuItem *item,
                                   gpointer data)
{
  if (gui_options.draw_map_grid ^ gtk_check_menu_item_get_active(item)) {
    key_map_grid_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_NATIONAL_BORDERS" callback.
****************************************************************************/
static void show_national_borders_callback(GtkCheckMenuItem *item,
                                           gpointer data)
{
  if (gui_options.draw_borders ^ gtk_check_menu_item_get_active(item)) {
    key_map_borders_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_NATIVE_TILES" callback.
****************************************************************************/
static void show_native_tiles_callback(GtkCheckMenuItem *item,
                                       gpointer data)
{
  if (gui_options.draw_native ^ gtk_check_menu_item_get_active(item)) {
    key_map_native_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_CITY_FULL_BAR" callback.
****************************************************************************/
static void show_city_full_bar_callback(GtkCheckMenuItem *item,
                                        gpointer data)
{
  if (gui_options.draw_full_citybar ^ gtk_check_menu_item_get_active(item)) {
    key_city_full_bar_toggle();
    view_menu_update_sensitivity();
  }
}

/************************************************************************//**
  Item "SHOW_CITY_NAMES" callback.
****************************************************************************/
static void show_city_names_callback(GtkCheckMenuItem *item,
                                     gpointer data)
{
  if (gui_options.draw_city_names ^ gtk_check_menu_item_get_active(item)) {
    key_city_names_toggle();
    view_menu_update_sensitivity();
  }
}

/************************************************************************//**
  Item "SHOW_CITY_GROWTH" callback.
****************************************************************************/
static void show_city_growth_callback(GtkCheckMenuItem *item,
                                      gpointer data)
{
  if (gui_options.draw_city_growth ^ gtk_check_menu_item_get_active(item)) {
    key_city_growth_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_CITY_PRODUCTIONS" callback.
****************************************************************************/
static void show_city_productions_callback(GtkCheckMenuItem *item,
                                           gpointer data)
{
  if (gui_options.draw_city_productions ^ gtk_check_menu_item_get_active(item)) {
    key_city_productions_toggle();
    view_menu_update_sensitivity();
  }
}

/************************************************************************//**
  Item "SHOW_CITY_BUY_COST" callback.
****************************************************************************/
static void show_city_buy_cost_callback(GtkCheckMenuItem *item,
                                        gpointer data)
{
  if (gui_options.draw_city_buycost ^ gtk_check_menu_item_get_active(item)) {
    key_city_buycost_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_CITY_TRADE_ROUTES" callback.
****************************************************************************/
static void show_city_trade_routes_callback(GtkCheckMenuItem *item,
                                            gpointer data)
{
  if (gui_options.draw_city_trade_routes ^ gtk_check_menu_item_get_active(item)) {
    key_city_trade_routes_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_TERRAIN" callback.
****************************************************************************/
static void show_terrain_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (gui_options.draw_terrain ^ gtk_check_menu_item_get_active(item)) {
    key_terrain_toggle();
    view_menu_update_sensitivity();
  }
}

/************************************************************************//**
  Item "SHOW_COASTLINE" callback.
****************************************************************************/
static void show_coastline_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (gui_options.draw_coastline ^ gtk_check_menu_item_get_active(item)) {
    key_coastline_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_PATHS" callback.
****************************************************************************/
static void show_paths_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (gui_options.draw_paths ^ gtk_check_menu_item_get_active(item)) {
    key_paths_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_IRRIGATION" callback.
****************************************************************************/
static void show_irrigation_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (gui_options.draw_irrigation ^ gtk_check_menu_item_get_active(item)) {
    key_irrigation_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_MINE" callback.
****************************************************************************/
static void show_mine_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (gui_options.draw_mines ^ gtk_check_menu_item_get_active(item)) {
    key_mines_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_BASES" callback.
****************************************************************************/
static void show_bases_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (gui_options.draw_fortress_airbase ^ gtk_check_menu_item_get_active(item)) {
    key_bases_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_RESOURCES" callback.
****************************************************************************/
static void show_resources_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (gui_options.draw_specials ^ gtk_check_menu_item_get_active(item)) {
    key_resources_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_HUTS" callback.
****************************************************************************/
static void show_huts_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (gui_options.draw_huts ^ gtk_check_menu_item_get_active(item)) {
    key_huts_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_POLLUTION" callback.
****************************************************************************/
static void show_pollution_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (gui_options.draw_pollution ^ gtk_check_menu_item_get_active(item)) {
    key_pollution_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_CITIES" callback.
****************************************************************************/
static void show_cities_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (gui_options.draw_cities ^ gtk_check_menu_item_get_active(item)) {
    key_cities_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_UNITS" callback.
****************************************************************************/
static void show_units_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (gui_options.draw_units ^ gtk_check_menu_item_get_active(item)) {
    key_units_toggle();
    view_menu_update_sensitivity();
  }
}

/************************************************************************//**
  Item "SHOW_UNIT_SOLID_BG" callback.
****************************************************************************/
static void show_unit_solid_bg_callback(GtkCheckMenuItem *item,
                                        gpointer data)
{
  if (gui_options.solid_color_behind_units ^ gtk_check_menu_item_get_active(item)) {
    key_unit_solid_bg_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_UNIT_SHIELDS" callback.
****************************************************************************/
static void show_unit_shields_callback(GtkCheckMenuItem *item,
                                       gpointer data)
{
  if (gui_options.draw_unit_shields ^ gtk_check_menu_item_get_active(item)) {
    key_unit_shields_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_STACK_SIZE" callback.
****************************************************************************/
static void show_stack_size_callback(GtkCheckMenuItem *item,
                                     gpointer data)
{
  if (gui_options.draw_unit_stack_size ^ gtk_check_menu_item_get_active(item)) {
    key_unit_stack_size_toggle();
  }
}

/************************************************************************//**
  Item "SHOW_FOCUS_UNIT" callback.
****************************************************************************/
static void show_focus_unit_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (gui_options.draw_focus_unit ^ gtk_check_menu_item_get_active(item)) {
    key_focus_unit_toggle();
    view_menu_update_sensitivity();
  }
}

/************************************************************************//**
  Item "SHOW_FOG_OF_WAR" callback.
****************************************************************************/
static void show_fog_of_war_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (gui_options.draw_fog_of_war ^ gtk_check_menu_item_get_active(item)) {
    key_fog_of_war_toggle();
    view_menu_update_sensitivity();
  }
}

/************************************************************************//**
  Item "FULL_SCREEN" callback.
****************************************************************************/
static void full_screen_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (GUI_GTK_OPTION(fullscreen) ^ gtk_check_menu_item_get_active(item)) {
    GUI_GTK_OPTION(fullscreen) ^= 1;

    fullscreen_opt_refresh(NULL);
  }
}

/************************************************************************//**
  Item "RECALC_BORDERS" callback.
****************************************************************************/
static void recalc_borders_callback(GtkMenuItem *item, gpointer data)
{
  key_editor_recalculate_borders();
}

/************************************************************************//**
  Item "TOGGLE_FOG" callback.
****************************************************************************/
static void toggle_fog_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (game.client.fog_of_war ^ gtk_check_menu_item_get_active(item)) {
    key_editor_toggle_fogofwar();
  }
}

/************************************************************************//**
  Item "SCENARIO_PROPERTIES" callback.
****************************************************************************/
static void scenario_properties_callback(GtkMenuItem *item, gpointer data)
{
  struct property_editor *pe;

  pe = editprop_get_property_editor();
  property_editor_reload(pe, OBJTYPE_GAME);
  property_editor_popup(pe, OBJTYPE_GAME);
}

/************************************************************************//**
  Item "SAVE_SCENARIO" callback.
****************************************************************************/
static void save_scenario_callback(GtkMenuItem *item, gpointer data)
{
  save_scenario_dialog_popup();
}

/************************************************************************//**
  Item "SELECT_SINGLE" callback.
****************************************************************************/
static void select_single_callback(GtkMenuItem *item, gpointer data)
{
  request_unit_select(get_units_in_focus(), SELTYPE_SINGLE, SELLOC_TILE);
}

/************************************************************************//**
  Item "SELECT_ALL_ON_TILE" callback.
****************************************************************************/
static void select_all_on_tile_callback(GtkMenuItem *item, gpointer data)
{
  request_unit_select(get_units_in_focus(), SELTYPE_ALL, SELLOC_TILE);
}

/************************************************************************//**
  Item "SELECT_SAME_TYPE_TILE" callback.
****************************************************************************/
static void select_same_type_tile_callback(GtkMenuItem *item, gpointer data)
{
  request_unit_select(get_units_in_focus(), SELTYPE_SAME, SELLOC_TILE);
}

/************************************************************************//**
  Item "SELECT_SAME_TYPE_CONT" callback.
****************************************************************************/
static void select_same_type_cont_callback(GtkMenuItem *item, gpointer data)
{
  request_unit_select(get_units_in_focus(), SELTYPE_SAME, SELLOC_CONT);
}

/************************************************************************//**
  Item "SELECT_SAME_TYPE" callback.
****************************************************************************/
static void select_same_type_callback(GtkMenuItem *item, gpointer data)
{
  request_unit_select(get_units_in_focus(), SELTYPE_SAME, SELLOC_WORLD);
}

/************************************************************************//**
  Open unit selection dialog.
****************************************************************************/
static void select_dialog_callback(GtkMenuItem *item, gpointer data)
{
  unit_select_dialog_popup(NULL);
}

/************************************************************************//**
  Open rally point dialog.
****************************************************************************/
static void rally_dialog_callback(GtkMenuItem *item, gpointer data)
{
  rally_dialog_popup();
}

/************************************************************************//**
  Open infra placement dialog.
****************************************************************************/
static void infra_dialog_callback(GtkMenuItem *item, gpointer data)
{
  infra_dialog_popup();
}

/************************************************************************//**
  Item "UNIT_WAIT" callback.
****************************************************************************/
static void unit_wait_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_wait();
}

/************************************************************************//**
  Item "UNIT_DONE" callback.
****************************************************************************/
static void unit_done_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_done();
}

/************************************************************************//**
  Item "UNIT_GOTO" callback.
****************************************************************************/
static void unit_goto_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_goto();
}

/************************************************************************//**
  Activate the goto system with an action to perform once there.
****************************************************************************/
static void unit_goto_and_callback(GtkMenuItem *item, gpointer data)
{
  int sub_target = NO_TARGET;
  struct action *paction = g_object_get_data(G_OBJECT(item), "end_action");

  fc_assert_ret(paction != NULL);

  switch (action_get_sub_target_kind(paction)) {
  case ASTK_BUILDING:
    {
      struct impr_type *pbuilding = g_object_get_data(G_OBJECT(item),
                                                      "end_building");
      fc_assert_ret(pbuilding != NULL);
      sub_target = improvement_number(pbuilding);
    }
    break;
  case ASTK_TECH:
    {
      struct advance *ptech = g_object_get_data(G_OBJECT(item),
                                                "end_tech");
      fc_assert_ret(ptech != NULL);
      sub_target = advance_number(ptech);
    }
    break;
  case ASTK_EXTRA:
  case ASTK_EXTRA_NOT_THERE:
    {
      struct extra_type *pextra = g_object_get_data(G_OBJECT(item),
                                                    "end_extra");
      fc_assert_ret(pextra != NULL);
      sub_target = extra_number(pextra);
    }
    break;
  case ASTK_SPECIALIST:
    {
      struct specialist *pspec = g_object_get_data(G_OBJECT(item),
                                                   "end_specialist");
      fc_assert_ret(nullptr != pspec);
      sub_target = specialist_number(pspec);
    }
    break;
  case ASTK_NONE:
    sub_target = NO_TARGET;
    break;
  case ASTK_COUNT:
    /* Should not exits */
    fc_assert(action_get_sub_target_kind(paction) != ASTK_COUNT);
    break;
  }

  request_unit_goto(ORDER_PERFORM_ACTION, paction->id, sub_target);
}

/************************************************************************//**
  Item "UNIT_GOTO_CITY" callback.
****************************************************************************/
static void unit_goto_city_callback(GtkMenuItem *item, gpointer data)
{
  if (get_num_units_in_focus() > 0) {
    popup_goto_dialog();
  }
}

/************************************************************************//**
  Item "UNIT_RETURN" callback.
****************************************************************************/
static void unit_return_callback(GtkMenuItem *item, gpointer data)
{
  unit_list_iterate(get_units_in_focus(), punit) {
    request_unit_return(punit);
  } unit_list_iterate_end;
}

/************************************************************************//**
  Item "UNIT_EXPLORE" callback.
****************************************************************************/
static void unit_explore_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_auto_explore();
}

/************************************************************************//**
  Item "UNIT_PATROL" callback.
****************************************************************************/
static void unit_patrol_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_patrol();
}

/************************************************************************//**
  Item "UNIT_SENTRY" callback.
****************************************************************************/
static void unit_sentry_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_sentry();
}

/************************************************************************//**
  Item "UNIT_UNSENTRY" callback.
****************************************************************************/
static void unit_unsentry_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_wakeup_others();
}

/************************************************************************//**
  Item "UNIT_BOARD" callback.
****************************************************************************/
static void unit_board_callback(GtkMenuItem *item, gpointer data)
{
  unit_list_iterate(get_units_in_focus(), punit) {
    request_transport(punit, unit_tile(punit));
  } unit_list_iterate_end;
}

/************************************************************************//**
  Item "UNIT_DEBOARD" callback.
****************************************************************************/
static void unit_deboard_callback(GtkMenuItem *item, gpointer data)
{
  unit_list_iterate(get_units_in_focus(), punit) {
    request_unit_unload(punit);
  } unit_list_iterate_end;
}

/************************************************************************//**
  Item "UNIT_UNLOAD_TRANSPORTER" callback.
****************************************************************************/
static void unit_unload_transporter_callback(GtkMenuItem *item,
                                             gpointer data)
{
  key_unit_unload_all();
}

/************************************************************************//**
  Item "UNIT_HOMECITY" callback.
****************************************************************************/
static void unit_homecity_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_homecity();
}

/************************************************************************//**
  Item "UNIT_UPGRADE" callback.
****************************************************************************/
static void unit_upgrade_callback(GtkMenuItem *item, gpointer data)
{
  popup_upgrade_dialog(get_units_in_focus());
}

/************************************************************************//**
  Item "UNIT_CONVERT" callback.
****************************************************************************/
static void unit_convert_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_convert();
}

/************************************************************************//**
  Item "UNIT_DISBAND" callback.
****************************************************************************/
static void unit_disband_callback(GtkMenuItem *item, gpointer data)
{
  popup_disband_dialog(get_units_in_focus());
}

/************************************************************************//**
  Item "BUILD_CITY" callback.
****************************************************************************/
static void build_city_callback(GtkMenuItem *item, gpointer data)
{
  unit_list_iterate(get_units_in_focus(), punit) {
    /* FIXME: this can provide different items for different units...
     * not good! */
    /* Enable the button for adding to a city in all cases, so we
       get an eventual error message from the server if we try. */
    if (unit_can_add_or_build_city(&(wld.map), punit)) {
      request_unit_build_city(punit);
    } else if (utype_can_do_action(unit_type_get(punit),
                                   ACTION_HELP_WONDER)) {
      request_unit_caravan_action(punit, ACTION_HELP_WONDER);
    }
  } unit_list_iterate_end;
}

/************************************************************************//**
  Action "AUTO_WORK" callback.
****************************************************************************/
static void auto_work_callback(GtkMenuItem *action, gpointer data)
{
  key_unit_auto_work();
}

/************************************************************************//**
  Action "BUILD_ROAD" callback.
****************************************************************************/
static void build_road_callback(GtkMenuItem *action, gpointer data)
{
  unit_list_iterate(get_units_in_focus(), punit) {
    /* FIXME: this can provide different actions for different units...
     * not good! */
    struct extra_type *tgt = next_extra_for_tile(unit_tile(punit),
                                                 EC_ROAD,
                                                 unit_owner(punit),
                                                 punit);
    bool building_road = FALSE;

    if (tgt != NULL
        && can_unit_do_activity_targeted_client(punit, ACTIVITY_GEN_ROAD, tgt)) {
      request_new_unit_activity_targeted(punit, ACTIVITY_GEN_ROAD, tgt);
      building_road = TRUE;
    }

    if (!building_road && unit_can_est_trade_route_here(punit)) {
      request_unit_caravan_action(punit, ACTION_TRADE_ROUTE);
    }
  } unit_list_iterate_end;
}

/************************************************************************//**
  Action "BUILD_IRRIGATION" callback.
****************************************************************************/
static void build_irrigation_callback(GtkMenuItem *action, gpointer data)
{
  key_unit_irrigate();
}

/************************************************************************//**
  Action "CULTIVATE" callback.
****************************************************************************/
static void cultivate_callback(GtkMenuItem *action, gpointer data)
{
  key_unit_cultivate();
}

/************************************************************************//**
  Action "BUILD_MINE" callback.
****************************************************************************/
static void build_mine_callback(GtkMenuItem *action, gpointer data)
{
  key_unit_mine();
}

/************************************************************************//**
  Action "PLANT" callback.
****************************************************************************/
static void plant_callback(GtkMenuItem *action, gpointer data)
{
  key_unit_plant();
}

/************************************************************************//**
  Action "CONNECT_ROAD" callback.
****************************************************************************/
static void connect_road_callback(GtkMenuItem *action, gpointer data)
{
  struct road_type *proad = road_by_gui_type(ROAD_GUI_ROAD);

  if (proad != NULL) {
    struct extra_type *tgt;

    tgt = road_extra_get(proad);

    key_unit_connect(ACTIVITY_GEN_ROAD, tgt);
  }
}

/************************************************************************//**
  Action "CONNECT_RAIL" callback.
****************************************************************************/
static void connect_rail_callback(GtkMenuItem *action, gpointer data)
{
  struct road_type *prail = road_by_gui_type(ROAD_GUI_RAILROAD);

  if (prail != NULL) {
    struct extra_type *tgt;

    tgt = road_extra_get(prail);

    key_unit_connect(ACTIVITY_GEN_ROAD, tgt);
  }
}

/************************************************************************//**
  Action "CONNECT_MAGLEV" callback.
****************************************************************************/
static void connect_maglev_callback(GtkMenuItem *action, gpointer data)
{
  struct road_type *pmaglev = road_by_gui_type(ROAD_GUI_MAGLEV);

  if (pmaglev != NULL) {
    struct extra_type *tgt;

    tgt = road_extra_get(pmaglev);

    key_unit_connect(ACTIVITY_GEN_ROAD, tgt);
  }
}

/************************************************************************//**
  Action "CONNECT_IRRIGATION" callback.
****************************************************************************/
static void connect_irrigation_callback(GtkMenuItem *action, gpointer data)
{
  struct extra_type_list *extras = extra_type_list_by_cause(EC_IRRIGATION);

  if (extra_type_list_size(extras) > 0) {
    struct extra_type *pextra;

    pextra = extra_type_list_get(extra_type_list_by_cause(EC_IRRIGATION), 0);

    key_unit_connect(ACTIVITY_IRRIGATE, pextra);
  }
}

/************************************************************************//**
  Action "TRANSFORM_TERRAIN" callback.
****************************************************************************/
static void transform_terrain_callback(GtkMenuItem *action, gpointer data)
{
  key_unit_transform();
}

/************************************************************************//**
  Action "CLEAN" callback.
****************************************************************************/
static void clean_nuisance_callback(GtkMenuItem *action, gpointer data)
{
  key_unit_clean();
}

/************************************************************************//**
  Action "BUILD_FORTRESS" callback.
****************************************************************************/
static void build_fortress_callback(GtkMenuItem *action, gpointer data)
{
  key_unit_fortress();
}

/************************************************************************//**
  Action "FORTIFY" callback.
****************************************************************************/
static void fortify_callback(GtkMenuItem *action, gpointer data)
{
  unit_list_iterate(get_units_in_focus(), punit) {
    request_unit_fortify(punit);
  } unit_list_iterate_end;
}

/************************************************************************//**
  Action "BUILD_AIRBASE" callback.
****************************************************************************/
static void build_airbase_callback(GtkMenuItem *action, gpointer data)
{
  key_unit_airbase();
}

/************************************************************************//**
  Action "DO_PARADROP" callback.
****************************************************************************/
static void do_paradrop_callback(GtkMenuItem *action, gpointer data)
{
  key_unit_paradrop();
}

/************************************************************************//**
  Action "DO_PILLAGE" callback.
****************************************************************************/
static void do_pillage_callback(GtkMenuItem *action, gpointer data)
{
  key_unit_pillage();
}

/************************************************************************//**
  Action "DIPLOMAT_ACTION" callback.
****************************************************************************/
static void diplomat_action_callback(GtkMenuItem *action, gpointer data)
{
  key_unit_action_select_tgt();
}

/************************************************************************//**
  Action "TAX_RATE" callback.
****************************************************************************/
static void tax_rate_callback(GtkMenuItem *action, gpointer data)
{
  popup_rates_dialog();
}

/************************************************************************//**
  Action "MULTIPLIERS" callback.
****************************************************************************/
static void multiplier_callback(GtkMenuItem *action, gpointer data)
{
  popup_multiplier_dialog();
}

/************************************************************************//**
  The player has chosen a government from the menu.
****************************************************************************/
static void government_callback(GtkMenuItem *item, gpointer data)
{
  popup_revolution_dialog((struct government *) data);
}

/************************************************************************//**
  The player has chosen a base to build from the menu.
****************************************************************************/
static void base_callback(GtkMenuItem *item, gpointer data)
{
  struct extra_type *pextra = data;

  unit_list_iterate(get_units_in_focus(), punit) {
    request_new_unit_activity_targeted(punit, ACTIVITY_BASE, pextra);
  } unit_list_iterate_end;
}

/************************************************************************//**
  The player has chosen a road to build from the menu.
****************************************************************************/
static void road_callback(GtkMenuItem *item, gpointer data)
{
  struct extra_type *pextra = data;

  unit_list_iterate(get_units_in_focus(), punit) {
    request_new_unit_activity_targeted(punit, ACTIVITY_GEN_ROAD,
                                       pextra);
  } unit_list_iterate_end;
}

/************************************************************************//**
  The player has chosen a nuisance to clean from the menu.
****************************************************************************/
static void clean_callback(GtkMenuItem *item, gpointer data)
{
  struct extra_type *pextra = data;

  unit_list_iterate(get_units_in_focus(), punit) {
    if (can_unit_do_activity_targeted_client(punit, ACTIVITY_CLEAN, pextra)) {
      request_new_unit_activity_targeted(punit, ACTIVITY_CLEAN,
                                         pextra);
    }
  } unit_list_iterate_end;
}

/************************************************************************//**
  Action "CENTER_VIEW" callback.
****************************************************************************/
static void center_view_callback(GtkMenuItem *action, gpointer data)
{
  center_on_unit();
}

/************************************************************************//**
  Action "REPORT_UNITS" callback.
****************************************************************************/
static void report_units_callback(GtkMenuItem *action, gpointer data)
{
  units_report_dialog_popup(TRUE);
}

/************************************************************************//**
  Action "REPORT_CITIES" callback.
****************************************************************************/
static void report_cities_callback(GtkMenuItem *action, gpointer data)
{
  city_report_dialog_popup(TRUE);
}

/************************************************************************//**
  Action "REPORT_ECONOMY" callback.
****************************************************************************/
static void report_economy_callback(GtkMenuItem *action, gpointer data)
{
  economy_report_dialog_popup(TRUE);
}

/************************************************************************//**
  Action "REPORT_RESEARCH" callback.
****************************************************************************/
static void report_research_callback(GtkMenuItem *action, gpointer data)
{
  science_report_dialog_popup(TRUE);
}

/************************************************************************//**
  Action "REPORT_SPACESHIP" callback.
****************************************************************************/
static void report_spaceship_callback(GtkMenuItem *action, gpointer data)
{
  if (NULL != client.conn.playing) {
    popup_spaceship_dialog(client.conn.playing);
  }
}

/************************************************************************//**
  Set name of the menu item.
****************************************************************************/
static void menu_entry_init(GtkBuildable *item)
{
  const char *key = gtk_buildable_get_name(item);
  struct menu_entry_info *info = menu_entry_info_find(key);

  if (info != NULL) {
    gtk_menu_item_set_label(GTK_MENU_ITEM(item),
                            Q_(info->name));
    gtk_menu_item_set_use_underline(GTK_MENU_ITEM(item), TRUE);
    if (info->cb != NULL) {
      g_signal_connect(item, "activate", info->cb, NULL);
    }

    if (info->accel != 0) {
      const char *path = gtk_menu_item_get_accel_path(GTK_MENU_ITEM(item));

      if (path == NULL) {
        char buf[512];

        fc_snprintf(buf, sizeof(buf), "<MENU>/%s", key);
        gtk_menu_item_set_accel_path(GTK_MENU_ITEM(item), buf);
        path = buf; /* Not NULL, but not usable either outside this block */
      }

      if (path != NULL) {
        gtk_accel_map_add_entry(gtk_menu_item_get_accel_path(GTK_MENU_ITEM(item)),
                                info->accel, info->accel_mod);
      }
    }

    return;
  }

  /* temporary naming solution */
  gtk_menu_item_set_label(GTK_MENU_ITEM(item), key);
}

/************************************************************************//**
  Returns the name of the file readable by the GtkUIManager.
****************************************************************************/
static const gchar *get_ui_filename(void)
{
  static char filename[256];
  const char *name;

  if ((name = getenv("FREECIV_MENUS"))
      || (name = fileinfoname(get_data_dirs(), "gtk3.22_menus.xml"))) {
    sz_strlcpy(filename, name);
  } else {
    log_error("Gtk menus: file definition not found");
    filename[0] = '\0';
  }

  log_verbose("ui menu file is \"%s\".", filename);
  return filename;
}

/************************************************************************//**
  Creates the menu bar.
****************************************************************************/
GtkWidget *setup_menus(GtkWidget *window)
{
  GtkWidget *menubar = NULL;
  GError *error = NULL;

  ui_builder = gtk_builder_new();
  if (!gtk_builder_add_from_file(ui_builder, get_ui_filename(), &error)) {
    log_error("Gtk menus: %s", error->message);
    g_error_free(error);
  } else {
    GSList *entries;
    GSList *next;

    entries = gtk_builder_get_objects(ui_builder);
    next = entries;

    while (next != NULL) {
      GObject *obj = next->data;

      if (GTK_IS_MENU_ITEM(obj)) {
        if (!GTK_IS_SEPARATOR_MENU_ITEM(obj)) {
          menu_entry_init(GTK_BUILDABLE(obj));
        }
      } else if (GTK_IS_MENU(obj)) {
        const char *key = gtk_buildable_get_name(GTK_BUILDABLE(obj));

        if (key[0] == '<') {
          GtkAccelGroup *ac_group = gtk_menu_get_accel_group(GTK_MENU(obj));

          if (ac_group == NULL) {
            ac_group = gtk_accel_group_new();
            gtk_menu_set_accel_group(GTK_MENU(obj), ac_group);
          }

          gtk_window_add_accel_group(GTK_WINDOW(window), ac_group);

          gtk_menu_set_accel_path(GTK_MENU(obj), key);
        }
      }

      next = next->next;
    }

    g_slist_free(entries);

    menubar = GTK_WIDGET(gtk_builder_get_object(ui_builder, "MENU"));
    gtk_widget_set_visible(menubar, TRUE);
    gtk_widget_show_all(menubar);
  }

#ifndef FREECIV_DEBUG
  menu_entry_set_visible("RELOAD_TILESET", FALSE, FALSE);
#endif /* FREECIV_DEBUG */

  return menubar;
}

/************************************************************************//**
  Find menu entry construction data
****************************************************************************/
static struct menu_entry_info *menu_entry_info_find(const char *key)
{
  int i;

  for (i = 0; menu_entries[i].key != NULL; i++) {
    if (!strcmp(key, menu_entries[i].key)) {
      return &(menu_entries[i]);
    }
  }

  return NULL;
}

/************************************************************************//**
  Sets an menu entry sensitive.
****************************************************************************/
static void menu_entry_set_active(const char *key,
                                  gboolean is_active)
{
  GtkCheckMenuItem *item = GTK_CHECK_MENU_ITEM(gtk_builder_get_object(ui_builder, key));

  if (item != NULL) {
    gtk_check_menu_item_set_active(item, is_active);
  }
}

/************************************************************************//**
  Sets sensitivity of an menu entry.
****************************************************************************/
static void menu_entry_set_sensitive(const char *key,
                                     gboolean is_sensitive)
{
  GtkWidget *item = GTK_WIDGET(gtk_builder_get_object(ui_builder, key));

  if (item != NULL) {
    gtk_widget_set_sensitive(item, is_sensitive);
  }
}

/************************************************************************//**
  Set sensitivity of all entries in the group.
****************************************************************************/
static void menu_entry_group_set_sensitive(enum menu_entry_grouping group,
                                           gboolean is_sensitive)
{
  int i;

  for (i = 0; menu_entries[i].key != NULL; i++) {
    if (menu_entries[i].grouping == group || group == MGROUP_ALL) {
      menu_entry_set_sensitive(menu_entries[i].key, is_sensitive);
    }
  }
}

/************************************************************************//**
  Sets an action visible.
****************************************************************************/
#ifndef FREECIV_DEBUG
static void menu_entry_set_visible(const char *key,
                                   gboolean is_visible,
                                   gboolean is_sensitive)
{
  GtkWidget *item = GTK_WIDGET(gtk_builder_get_object(ui_builder, key));

  if (item != NULL) {
    gtk_widget_set_visible(item, is_visible);
    gtk_widget_set_sensitive(item, is_sensitive);
  }
}
#endif /* FREECIV_DEBUG */

/************************************************************************//**
  Renames an action.
****************************************************************************/
static void menus_rename(const char *key,
                         const gchar *new_label)
{
  GtkWidget *item = GTK_WIDGET(gtk_builder_get_object(ui_builder, key));

  if (item != NULL) {
    gtk_menu_item_set_label(GTK_MENU_ITEM(item), new_label);
  }
}

/************************************************************************//**
  Find the child menu of an action.
****************************************************************************/
static GtkMenu *find_menu(const char *key)
{
  return GTK_MENU(gtk_builder_get_object(ui_builder, key));
}

/************************************************************************//**
  Update the sensitivity of the items in the view menu.
****************************************************************************/
static void view_menu_update_sensitivity(void)
{
  /* The "full" city bar (i.e. the new way of drawing the
   * city name), can draw the city growth even without drawing
   * the city name. But the old method cannot. */
  if (gui_options.draw_full_citybar) {
    menu_entry_set_sensitive("SHOW_CITY_GROWTH", TRUE);
    menu_entry_set_sensitive("SHOW_CITY_TRADE_ROUTES", TRUE);
  } else {
    menu_entry_set_sensitive("SHOW_CITY_GROWTH", gui_options.draw_city_names);
    menu_entry_set_sensitive("SHOW_CITY_TRADE_ROUTES",
                             gui_options.draw_city_names);
  }

  menu_entry_set_sensitive("SHOW_CITY_BUY_COST",
                           gui_options.draw_city_productions);
  menu_entry_set_sensitive("SHOW_COASTLINE", !gui_options.draw_terrain);
  menu_entry_set_sensitive("SHOW_UNIT_SOLID_BG",
                           gui_options.draw_units || gui_options.draw_focus_unit);
  menu_entry_set_sensitive("SHOW_UNIT_SHIELDS",
                           gui_options.draw_units || gui_options.draw_focus_unit);
  menu_entry_set_sensitive("SHOW_STACK_SIZE",
                           gui_options.draw_units || gui_options.draw_focus_unit);
  menu_entry_set_sensitive("SHOW_FOCUS_UNIT", !gui_options.draw_units);
}

/************************************************************************//**
  Return the text for the tile, changed by the activity.

  Should only be called for irrigation, mining, or transformation, and
  only when the activity changes the base terrain type.
****************************************************************************/
static const char *get_tile_change_menu_text(struct tile *ptile,
                                             enum unit_activity activity)
{
  struct tile *newtile = tile_virtual_new(ptile);
  const char *text;

  tile_apply_activity(newtile, activity, NULL);
  text = tile_get_info_text(newtile, FALSE, 0);
  tile_virtual_destroy(newtile);
  return text;
}

/************************************************************************//**
  Updates the menus.
****************************************************************************/
void real_menus_update(void)
{
  struct unit_list *punits = NULL;
  bool units_all_same_tile = TRUE, units_all_same_type = TRUE;
  GtkMenu *menu;
  char acttext[128], irrtext[128], mintext[128], transtext[128];
  char cultext[128], plantext[128];
  struct terrain *pterrain;
  bool conn_possible;
  struct road_type *proad;
  struct extra_type_list *extras;

  if (ui_builder == NULL || !can_client_change_view()) {
    return;
  }

  if (get_num_units_in_focus() > 0) {
    const struct tile *ptile = NULL;
    const struct unit_type *ptype = NULL;

    punits = get_units_in_focus();
    unit_list_iterate(punits, punit) {
      fc_assert((ptile == NULL) == (ptype == NULL));

      if (ptile || ptype) {
        if (unit_tile(punit) != ptile) {
          units_all_same_tile = FALSE;
        }
        if (unit_type_get(punit) != ptype) {
          units_all_same_type = FALSE;
        }
      } else {
        ptile = unit_tile(punit);
        ptype = unit_type_get(punit);
      }
    } unit_list_iterate_end;
  }

  menu_entry_group_set_sensitive(MGROUP_EDIT, editor_is_active());
  menu_entry_group_set_sensitive(MGROUP_PLAYING, can_client_issue_orders()
                                 && !editor_is_active());
  menu_entry_group_set_sensitive(MGROUP_UNIT, can_client_issue_orders()
                                 && !editor_is_active() && punits != NULL);

  menu_entry_set_active("EDIT_MODE", game.info.is_edit_mode);
  menu_entry_set_sensitive("EDIT_MODE",
                           can_conn_enable_editing(&client.conn));
  menu_entry_set_active("TOGGLE_FOG", game.client.fog_of_war);
  editgui_refresh();
  menu_entry_set_sensitive("RALLY_DLG", can_client_issue_orders());
  menu_entry_set_sensitive("INFRA_DLG", terrain_control.infrapoints);

  {
    char road_buf[500];

    proad = road_by_gui_type(ROAD_GUI_ROAD);
    if (proad != NULL) {
      /* TRANS: Connect with some road type (Road/Railroad) */
      snprintf(road_buf, sizeof(road_buf), _("Connect With %s"),
               extra_name_translation(road_extra_get(proad)));
      menus_rename("CONNECT_ROAD", road_buf);
    }

    proad = road_by_gui_type(ROAD_GUI_RAILROAD);
    if (proad != NULL) {
      snprintf(road_buf, sizeof(road_buf), _("Connect With %s"),
               extra_name_translation(road_extra_get(proad)));
      menus_rename("CONNECT_RAIL", road_buf);
    }

    proad = road_by_gui_type(ROAD_GUI_MAGLEV);
    if (proad != NULL) {
      snprintf(road_buf, sizeof(road_buf), _("Connect With %s"),
               extra_name_translation(road_extra_get(proad)));
      menus_rename("CONNECT_MAGLEV", road_buf);
    }
  }

  {
    char top_cities_buf[128];

    if (game.info.top_cities_count > 0) {
      fc_snprintf(top_cities_buf, sizeof(top_cities_buf),
                  PL_("Top %d City", "Top %d Cities",
                      game.info.top_cities_count),
                  game.info.top_cities_count);
      menus_rename("REPORT_TOP_CITIES", top_cities_buf);
    } else {
      menus_rename("REPORT_TOP_CITIES", _("Top Cities"));
    }

    menu_entry_set_sensitive("REPORT_TOP_CITIES", game.info.top_cities_count > 0);
  }

  menu_entry_set_sensitive("REPORT_SPACESHIP",
                           client_player() != NULL && client_player()->spaceship.state != SSHIP_NONE);

  if (!can_client_issue_orders()) {
    return;
  }

  /* Set government sensitivity. */
  if ((menu = find_menu("<MENU>/GOVERNMENT"))) {
    GList *list, *iter;
    struct government *pgov;

    list = gtk_container_get_children(GTK_CONTAINER(menu));
    for (iter = list; NULL != iter; iter = g_list_next(iter)) {
      pgov = g_object_get_data(G_OBJECT(iter->data), "government");
      if (NULL != pgov) {
        gtk_widget_set_sensitive(GTK_WIDGET(iter->data),
                                 can_change_to_government(client_player(),
                                                          pgov));
      } else {
        /* Revolution without target government */
        gtk_widget_set_sensitive(GTK_WIDGET(iter->data),
                                 untargeted_revolution_allowed());
      }
    }
    g_list_free(list);
  }

  if (!punits) {
    return;
  }

  /* Remaining part of this function: Update Unit, Work, and Combat menus */

  /* Set base sensitivity. */
  if ((menu = find_menu("<MENU>/BUILD_BASE"))) {
    GList *list, *iter;
    struct extra_type *pextra;

    list = gtk_container_get_children(GTK_CONTAINER(menu));
    for (iter = list; NULL != iter; iter = g_list_next(iter)) {
      pextra = g_object_get_data(G_OBJECT(iter->data), "base");
      if (NULL != pextra) {
        gtk_widget_set_sensitive(GTK_WIDGET(iter->data),
                                 can_units_do_activity_targeted_client(punits,
                                                                       ACTIVITY_BASE,
                                                                       pextra));
      }
    }
    g_list_free(list);
  }

  /* Set road sensitivity. */
  if ((menu = find_menu("<MENU>/BUILD_PATH"))) {
    GList *list, *iter;
    struct extra_type *pextra;

    list = gtk_container_get_children(GTK_CONTAINER(menu));
    for (iter = list; NULL != iter; iter = g_list_next(iter)) {
      pextra = g_object_get_data(G_OBJECT(iter->data), "road");
      if (NULL != pextra) {
        gtk_widget_set_sensitive(GTK_WIDGET(iter->data),
                                 can_units_do_activity_targeted_client(punits,
                                                                       ACTIVITY_GEN_ROAD,
                                                                       pextra));
      }
    }
    g_list_free(list);
  }

  /* Set cleaning menu sensitivity. */
  if ((menu = find_menu("<MENU>/CLEAN"))) {
    GList *list, *iter;
    struct extra_type *pextra;

    list = gtk_container_get_children(GTK_CONTAINER(menu));
    for (iter = list; NULL != iter; iter = g_list_next(iter)) {
      pextra = g_object_get_data(G_OBJECT(iter->data), "nuisance");
      if (NULL != pextra) {
        gtk_widget_set_sensitive(GTK_WIDGET(iter->data),
                                 can_units_do_activity_targeted_client(punits,
                                                                       ACTIVITY_CLEAN,
                                                                       pextra));
      }
    }
    g_list_free(list);
  }

  /* Set Go to and... action visibility. */
  if ((menu = find_menu("<MENU>/GOTO_AND"))) {
    GList *list, *iter;
    struct action *paction;

    bool can_do_something = FALSE;

    /* Enable a menu item if it is theoretically possible that one of the
     * selected units can perform it. Checking if the action can be performed
     * at the current tile is pointless since it should be performed at the
     * target tile. */
    list = gtk_container_get_children(GTK_CONTAINER(menu));
    for (iter = list; NULL != iter; iter = g_list_next(iter)) {
      paction = g_object_get_data(G_OBJECT(iter->data), "end_action");
      if (NULL != paction) {
        if (units_can_do_action(get_units_in_focus(),
                                paction->id, TRUE)) {
          gtk_widget_set_visible(GTK_WIDGET(iter->data), TRUE);
          gtk_widget_set_sensitive(GTK_WIDGET(iter->data), TRUE);
          can_do_something = TRUE;
        } else {
          gtk_widget_set_visible(GTK_WIDGET(iter->data), FALSE);
          gtk_widget_set_sensitive(GTK_WIDGET(iter->data), FALSE);
        }
      }
    }
    g_list_free(list);

    /* Only sensitive if an action may be possible. */
    menu_entry_set_sensitive("MENU_GOTO_AND", can_do_something);
  }

  /* Enable the button for adding to a city in all cases, so we
   * get an eventual error message from the server if we try. */
  menu_entry_set_sensitive("BUILD_CITY",
                           (can_units_do_on_map(&(wld.map), punits,
                                                unit_can_add_or_build_city)
             || can_units_do_on_map(&(wld.map), punits, unit_can_help_build_wonder_here)));
  menu_entry_set_sensitive("BUILD_ROAD",
                           (can_units_do_any_road(&(wld.map), punits)
                            || can_units_do(punits,
                                            unit_can_est_trade_route_here)));
  menu_entry_set_sensitive("BUILD_IRRIGATION",
                           can_units_do_activity_client(punits, ACTIVITY_IRRIGATE));
  menu_entry_set_sensitive("CULTIVATE",
                           can_units_do_activity_client(punits, ACTIVITY_CULTIVATE));
  menu_entry_set_sensitive("BUILD_MINE",
                           can_units_do_activity_client(punits, ACTIVITY_MINE));
  menu_entry_set_sensitive("PLANT",
                           can_units_do_activity_client(punits, ACTIVITY_PLANT));
  menu_entry_set_sensitive("TRANSFORM_TERRAIN",
                           can_units_do_activity_client(punits, ACTIVITY_TRANSFORM));
  menu_entry_set_sensitive("FORTIFY",
                           can_units_do_activity_client(punits,
                                                        ACTIVITY_FORTIFYING));
  menu_entry_set_sensitive("BUILD_FORTRESS",
                           can_units_do_base_gui(punits, BASE_GUI_FORTRESS));
  menu_entry_set_sensitive("BUILD_AIRBASE",
                           can_units_do_base_gui(punits, BASE_GUI_AIRBASE));
  menu_entry_set_sensitive("CLEAN",
                           can_units_do_activity_client(punits, ACTIVITY_CLEAN));
  menu_entry_set_sensitive("UNIT_SENTRY",
                           can_units_do_activity_client(punits, ACTIVITY_SENTRY));
  menu_entry_set_sensitive("DO_PARADROP",
                           can_units_do_on_map(&(wld.map), punits, can_unit_paradrop));
  /* FIXME: should conditionally rename "Pillage" to "Pillage..." in cases
   * where selecting the command results in a dialog box listing options of
   * what to pillage */
  menu_entry_set_sensitive("DO_PILLAGE",
                           can_units_do_activity_client(punits, ACTIVITY_PILLAGE));
  menu_entry_set_sensitive("UNIT_DISBAND",
                           units_can_do_action(punits, ACTION_DISBAND_UNIT,
                                               TRUE));
  menu_entry_set_sensitive("UNIT_UPGRADE",
                           units_can_upgrade(&(wld.map), punits));
  /* "UNIT_CONVERT" dealt with below */
  menu_entry_set_sensitive("UNIT_HOMECITY",
                           can_units_do_on_map(&(wld.map), punits, can_unit_change_homecity));
  menu_entry_set_sensitive("UNIT_UNLOAD_TRANSPORTER",
                           units_are_occupied(punits));
  menu_entry_set_sensitive("UNIT_BOARD",
                           units_can_load(punits));
  menu_entry_set_sensitive("UNIT_DEBOARD",
                           units_can_unload(&(wld.map), punits));
  menu_entry_set_sensitive("UNIT_UNSENTRY",
                           units_have_activity_on_tile(punits,
                                                       ACTIVITY_SENTRY));
  menu_entry_set_sensitive("AUTO_WORKER",
                           can_units_do(punits, can_unit_do_autoworker));
  menu_entry_set_sensitive("UNIT_EXPLORE",
                           can_units_do_activity_client(punits, ACTIVITY_EXPLORE));

  proad = road_by_gui_type(ROAD_GUI_ROAD);
  if (proad != NULL) {
    struct extra_type *tgt;

    tgt = road_extra_get(proad);

    conn_possible = can_units_do_connect(punits, ACTIVITY_GEN_ROAD, tgt);
  } else {
    conn_possible = FALSE;
  }
  menu_entry_set_sensitive("CONNECT_ROAD", conn_possible);

  proad = road_by_gui_type(ROAD_GUI_RAILROAD);
  if (proad != NULL) {
    struct extra_type *tgt;

    tgt = road_extra_get(proad);

    conn_possible = can_units_do_connect(punits, ACTIVITY_GEN_ROAD, tgt);
  } else {
    conn_possible = FALSE;
  }
  menu_entry_set_sensitive("CONNECT_RAIL", conn_possible);

  proad = road_by_gui_type(ROAD_GUI_MAGLEV);
  if (proad != NULL) {
    struct extra_type *tgt;

    tgt = road_extra_get(proad);

    conn_possible = can_units_do_connect(punits, ACTIVITY_GEN_ROAD, tgt);
  } else {
    conn_possible = FALSE;
  }
  menu_entry_set_sensitive("CONNECT_MAGLEV", conn_possible);

  extras = extra_type_list_by_cause(EC_IRRIGATION);

  if (extra_type_list_size(extras) > 0) {
    struct extra_type *tgt;

    tgt = extra_type_list_get(extras, 0);
    conn_possible = can_units_do_connect(punits, ACTIVITY_IRRIGATE, tgt);
  } else {
    conn_possible = FALSE;
  }
  menu_entry_set_sensitive("CONNECT_IRRIGATION", conn_possible);

  menu_entry_set_sensitive("DIPLOMAT_ACTION",
                           units_can_do_action(punits, ACTION_ANY, TRUE));

  if (units_can_do_action(punits, ACTION_HELP_WONDER, TRUE)) {
    menus_rename("BUILD_CITY",
                 action_get_ui_name_mnemonic(ACTION_HELP_WONDER, "_"));
  } else {
    bool city_on_tile = FALSE;

    /* FIXME: this overloading doesn't work well with multiple focus
     * units. */
    unit_list_iterate(punits, punit) {
      if (tile_city(unit_tile(punit))) {
        city_on_tile = TRUE;
        break;
      }
    } unit_list_iterate_end;

    if (city_on_tile && units_can_do_action(punits, ACTION_JOIN_CITY,
                                            TRUE)) {
      menus_rename("BUILD_CITY",
                   action_get_ui_name_mnemonic(ACTION_JOIN_CITY, "_"));
    } else {
      /* refresh default order */
      menus_rename("BUILD_CITY",
                   action_get_ui_name_mnemonic(ACTION_FOUND_CITY, "_"));
    }
  }

  if (units_can_do_action(punits, ACTION_TRADE_ROUTE, TRUE)) {
    menus_rename("BUILD_ROAD",
                 action_get_ui_name_mnemonic(ACTION_TRADE_ROUTE, "_"));
  } else if (units_have_type_flag(punits, UTYF_WORKERS, TRUE)) {
    char road_item[500];
    struct extra_type *pextra = NULL;

    /* FIXME: this overloading doesn't work well with multiple focus
     * units. */
    unit_list_iterate(punits, punit) {
      pextra = next_extra_for_tile(unit_tile(punit), EC_ROAD,
                                   unit_owner(punit), punit);
      if (pextra != NULL) {
        break;
      }
    } unit_list_iterate_end;

    if (pextra != NULL) {
      /* TRANS: Build road of specific type (Road/Railroad/Maglev) */
      snprintf(road_item, sizeof(road_item), _("Build %s"),
               extra_name_translation(pextra));
      menus_rename("BUILD_ROAD", road_item);
    }
  } else {
    menus_rename("BUILD_ROAD", _("Build _Road"));
  }

  if (units_all_same_type) {
    struct unit *punit = unit_list_get(punits, 0);
    const struct unit_type *to_unittype =
      can_upgrade_unittype(client_player(), unit_type_get(punit));
    if (to_unittype) {
      /* TRANS: %s is a unit type. */
      fc_snprintf(acttext, sizeof(acttext), _("Upgr_ade to %s"),
                  utype_name_translation(
                    can_upgrade_unittype(client_player(),
                                         unit_type_get(punit))));
    } else {
      acttext[0] = '\0';
    }
  } else {
    acttext[0] = '\0';
  }
  if ('\0' != acttext[0]) {
    menus_rename("UNIT_UPGRADE", acttext);
  } else {
    menus_rename("UNIT_UPGRADE", _("Upgr_ade"));
  }

  if (units_can_convert(&(wld.map), punits)) {
    menu_entry_set_sensitive("UNIT_CONVERT", TRUE);
    if (units_all_same_type) {
      struct unit *punit = unit_list_get(punits, 0);

      /* TRANS: %s is a unit type. */
      fc_snprintf(acttext, sizeof(acttext), _("C_onvert to %s"),
                  utype_name_translation(unit_type_get(punit)->converted_to));
    } else {
      acttext[0] = '\0';
    }
  } else {
    menu_entry_set_sensitive("UNIT_CONVERT", FALSE);
    acttext[0] = '\0';
  }
  if ('\0' != acttext[0]) {
    menus_rename("UNIT_CONVERT", acttext);
  } else {
    menus_rename("UNIT_CONVERT", _("C_onvert"));
  }

  if (units_all_same_tile) {
    struct unit *first = unit_list_get(punits, 0);

    pterrain = tile_terrain(unit_tile(first));

    if (units_have_type_flag(punits, UTYF_WORKERS, TRUE)) {
      struct extra_type *pextra = NULL;

      /* FIXME: this overloading doesn't work well with multiple focus
       * units. */
      unit_list_iterate(punits, punit) {
        pextra = next_extra_for_tile(unit_tile(punit), EC_IRRIGATION,
                                     unit_owner(punit), punit);
        if (pextra != NULL) {
          break;
        }
      } unit_list_iterate_end;

      if (pextra != NULL) {
        /* TRANS: Build irrigation of specific type */
        snprintf(irrtext, sizeof(irrtext), _("Build %s"),
                 extra_name_translation(pextra));
      } else {
        sz_strlcpy(irrtext, _("Build _Irrigation"));
      }
    } else {
      sz_strlcpy(irrtext, _("Build _Irrigation"));
    }

    if (pterrain->cultivate_result != T_NONE) {
      fc_snprintf(cultext, sizeof(cultext), _("Change to %s"),
                  get_tile_change_menu_text(unit_tile(first),
                                            ACTIVITY_CULTIVATE));
    } else {
      sz_strlcpy(cultext, _("_Cultivate"));
    }

    if (units_have_type_flag(punits, UTYF_WORKERS, TRUE)) {
      struct extra_type *pextra = NULL;

      /* FIXME: this overloading doesn't work well with multiple focus
       * units. */
      unit_list_iterate(punits, punit) {
        pextra = next_extra_for_tile(unit_tile(punit), EC_MINE,
                                     unit_owner(punit), punit);
        if (pextra != NULL) {
          break;
        }
      } unit_list_iterate_end;

      if (pextra != NULL) {
        /* TRANS: Build mine of specific type */
        snprintf(mintext, sizeof(mintext), _("Build %s"),
                 extra_name_translation(pextra));
      } else {
        sz_strlcpy(mintext, _("Build _Mine"));
      }
    } else {
      sz_strlcpy(mintext, _("Build _Mine"));
    }

    if (pterrain->plant_result != T_NONE) {
      fc_snprintf(plantext, sizeof(plantext), _("Change to %s"),
                  get_tile_change_menu_text(unit_tile(first), ACTIVITY_PLANT));
    } else {
      sz_strlcpy(plantext, _("_Plant"));
    }

    if (pterrain->transform_result != T_NONE
        && pterrain->transform_result != pterrain) {
      fc_snprintf(transtext, sizeof(transtext), _("Transf_orm to %s"),
                  get_tile_change_menu_text(unit_tile(first),
                                            ACTIVITY_TRANSFORM));
    } else {
      sz_strlcpy(transtext, _("Transf_orm Terrain"));
    }
  } else {
    sz_strlcpy(irrtext, _("Build _Irrigation"));
    sz_strlcpy(cultext, _("_Cultivate"));
    sz_strlcpy(mintext, _("Build _Mine"));
    sz_strlcpy(plantext, _("_Plant"));
    sz_strlcpy(transtext, _("Transf_orm Terrain"));
  }

  menus_rename("BUILD_IRRIGATION", irrtext);
  menus_rename("CULTIVATE", cultext);
  menus_rename("BUILD_MINE", mintext);
  menus_rename("PLANT", plantext);
  menus_rename("TRANSFORM_TERRAIN", transtext);

  menus_rename("UNIT_HOMECITY",
               action_get_ui_name_mnemonic(ACTION_HOME_CITY, "_"));
}

/************************************************************************//**
  Add an accelerator to an item in the "Go to and..." menu.
****************************************************************************/
static void menu_unit_goto_and_add_accel(GtkWidget *item, action_id act_id,
                                         const guint accel_key,
                                         const GdkModifierType accel_mods)
{
  const char *path = gtk_menu_item_get_accel_path(GTK_MENU_ITEM(item));

  if (path == NULL) {
    char buf[MAX_LEN_NAME + sizeof("<MENU>/GOTO_AND/")];

    fc_snprintf(buf, sizeof(buf), "<MENU>/GOTO_AND/%s",
                action_id_rule_name(act_id));
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(item), buf);
    path = buf; /* Not NULL, but not usable either outside this block */
  }

  if (path != NULL) {
    gtk_accel_map_add_entry(gtk_menu_item_get_accel_path(GTK_MENU_ITEM(item)),
                            accel_key, accel_mods);
  }
}

/************************************************************************//**
  Recursively remove previous entries in a menu and its sub menus.
****************************************************************************/
static void menu_remove_previous_entries(GtkMenu *menu)
{
  GList *list, *iter;
  GtkWidget *sub_menu;

  list = gtk_container_get_children(GTK_CONTAINER(menu));
  for (iter = list; NULL != iter; iter = g_list_next(iter)) {
    if ((sub_menu = gtk_menu_item_get_submenu(iter->data)) != NULL) {
      menu_remove_previous_entries(GTK_MENU(sub_menu));
      gtk_widget_destroy(sub_menu);
    }
    gtk_widget_destroy(GTK_WIDGET(iter->data));
  }
  g_list_free(list);
}

/************************************************************************//**
  Initialize menus (sensitivity, name, etc.) based on the
  current state and current ruleset, etc.  Call menus_update().
****************************************************************************/
void real_menus_init(void)
{
  GtkMenu *menu;

  if (ui_builder == NULL) {
    return;
  }

  menu_entry_set_sensitive("GAME_SAVE_AS",
                           can_client_access_hack()
                           && C_S_RUNNING <= client_state());
  menu_entry_set_sensitive("GAME_SAVE",
                           can_client_access_hack()
                           && C_S_RUNNING <= client_state());

  menu_entry_set_active("SAVE_OPTIONS_ON_EXIT",
                        gui_options.save_options_on_exit);
  menu_entry_set_sensitive("SERVER_OPTIONS", client.conn.established);

  menu_entry_set_sensitive("LEAVE",
                           client.conn.established);

  if (!can_client_change_view()) {
    menu_entry_group_set_sensitive(MGROUP_ALL, FALSE);

    return;
  }

  menus_rename("BUILD_FORTRESS", Q_(terrain_control.gui_type_base0));
  menus_rename("BUILD_AIRBASE", Q_(terrain_control.gui_type_base1));

  if ((menu = find_menu("<MENU>/GOVERNMENT"))) {
    GList *list, *iter;
    GtkWidget *item;
    char buf[256];

    /* Remove previous government entries. */
    list = gtk_container_get_children(GTK_CONTAINER(menu));
    for (iter = list; NULL != iter; iter = g_list_next(iter)) {
      if (g_object_get_data(G_OBJECT(iter->data), "government") != NULL
          || GTK_IS_SEPARATOR_MENU_ITEM(iter->data)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
      }
    }
    g_list_free(list);

    /* Add new government entries. */
    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);

    governments_iterate(g) {
      if (g != game.government_during_revolution) {
        /* TRANS: %s is a government name */
        fc_snprintf(buf, sizeof(buf), _("%s..."),
                    government_name_translation(g));
        item = gtk_menu_item_new_with_label(buf);
        g_object_set_data(G_OBJECT(item), "government", g);
        g_signal_connect(item, "activate",
                         G_CALLBACK(government_callback), g);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_show(item);
      }
    } governments_iterate_end;
  }

  if ((menu = find_menu("<MENU>/BUILD_BASE"))) {
    GList *list, *iter;
    GtkWidget *item;

    /* Remove previous base entries. */
    list = gtk_container_get_children(GTK_CONTAINER(menu));
    for (iter = list; NULL != iter; iter = g_list_next(iter)) {
      gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(list);

    /* Add new base entries. */
    extra_type_by_cause_iterate(EC_BASE, pextra) {
      if (pextra->buildable) {
        item = gtk_menu_item_new_with_label(extra_name_translation(pextra));
        g_object_set_data(G_OBJECT(item), "base", pextra);
        g_signal_connect(item, "activate", G_CALLBACK(base_callback), pextra);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_show(item);
      }
    } extra_type_by_cause_iterate_end;
  }

  if ((menu = find_menu("<MENU>/BUILD_PATH"))) {
    GList *list, *iter;
    GtkWidget *item;

    /* Remove previous road entries. */
    list = gtk_container_get_children(GTK_CONTAINER(menu));
    for (iter = list; NULL != iter; iter = g_list_next(iter)) {
      gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(list);

    /* Add new road entries. */
    extra_type_by_cause_iterate(EC_ROAD, pextra) {
      if (pextra->buildable) {
        item = gtk_menu_item_new_with_label(extra_name_translation(pextra));
        g_object_set_data(G_OBJECT(item), "road", pextra);
        g_signal_connect(item, "activate", G_CALLBACK(road_callback), pextra);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_show(item);
      }
    } extra_type_by_cause_iterate_end;
  }

  if ((menu = find_menu("<MENU>/CLEAN"))) {
    GList *list, *iter;
    GtkWidget *item;

    /* Remove previous cleaning entries. */
    list = gtk_container_get_children(GTK_CONTAINER(menu));
    for (iter = list; NULL != iter; iter = g_list_next(iter)) {
      gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(list);

    /* Add new cleaning entries. */
    extra_type_by_rmcause_iterate(ERM_CLEAN, pextra) {
      item = gtk_menu_item_new_with_label(extra_name_translation(pextra));
      g_object_set_data(G_OBJECT(item), "nuisance", pextra);
      g_signal_connect(item, "activate", G_CALLBACK(clean_callback), pextra);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
      gtk_widget_show(item);
    } extra_type_by_rmcause_iterate_end;
  }

  /* Initialize the Go to and... actions. */
  if ((menu = find_menu("<MENU>/GOTO_AND"))) {
    GtkWidget *item;
    int tgt_kind_group;

    /* Remove previous action entries. */
    menu_remove_previous_entries(menu);

    /* Add the new action entries grouped by target kind. */
    for (tgt_kind_group = 0; tgt_kind_group < ATK_COUNT; tgt_kind_group++) {
      action_noninternal_iterate(act_id) {
        struct action *paction = action_by_number(act_id);

        if (action_id_get_actor_kind(act_id) != AAK_UNIT) {
          /* This action isn't performed by a unit. */
          continue;
        }

        if (action_id_get_target_kind(act_id) != tgt_kind_group) {
          /* Wrong group. */
          continue;
        }

        /* Create and add the menu item. It will be hidden or shown based on
         * unit type.  */
        item = gtk_menu_item_new_with_mnemonic(
              action_get_ui_name_mnemonic(act_id, "_"));
        g_object_set_data(G_OBJECT(item), "end_action", paction);

        if (action_id_has_complex_target(act_id)) {
          GtkWidget *sub_target_menu = gtk_menu_new();

#define CREATE_SUB_ITEM(_sub_target_, _sub_target_key_, _sub_target_name_) \
  GtkWidget *sub_item = gtk_menu_item_new_with_label(_sub_target_name_);   \
  g_object_set_data(G_OBJECT(sub_item), "end_action", paction);            \
  g_object_set_data(G_OBJECT(sub_item), _sub_target_key_, _sub_target_);   \
  g_signal_connect(sub_item, "activate",                                   \
                   G_CALLBACK(unit_goto_and_callback), paction);           \
  gtk_menu_shell_append(GTK_MENU_SHELL(sub_target_menu), sub_item);        \
  gtk_widget_show(sub_item);

          switch (action_get_sub_target_kind(paction)) {
          case ASTK_BUILDING:
            improvement_iterate(pimpr) {
              CREATE_SUB_ITEM(pimpr, "end_building",
                              improvement_name_translation(pimpr));
            } improvement_iterate_end;
            break;
          case ASTK_TECH:
            advance_iterate(ptech) {
              CREATE_SUB_ITEM(ptech, "end_tech",
                              advance_name_translation(ptech));
            } advance_iterate_end;
            break;
          case ASTK_EXTRA:
          case ASTK_EXTRA_NOT_THERE:
            extra_type_iterate(pextra) {
              if (!actres_creates_extra(paction->result, pextra)
                  && !actres_removes_extra(paction->result, pextra)) {
                /* Not relevant */
                continue;
              }
              CREATE_SUB_ITEM(pextra, "end_extra",
                              extra_name_translation(pextra));
            } extra_type_iterate_end;
            break;
          case ASTK_SPECIALIST:
            specialist_type_iterate(spc) {
              struct specialist *pspec = specialist_by_number(spc);

              CREATE_SUB_ITEM(pspec, "end_specialist",
                              specialist_plural_translation(pspec));
            } specialist_type_iterate_end;
            break;
          case ASTK_NONE:
            /* Should not be here. */
            fc_assert(action_get_sub_target_kind(paction) != ASTK_NONE);
            break;
          case ASTK_COUNT:
            /* Should not exits */
            fc_assert(action_get_sub_target_kind(paction) != ASTK_COUNT);
            break;
          }

#undef CREATE_SUB_ITEM

          gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub_target_menu);
        } else {
          g_signal_connect(item, "activate",
                           G_CALLBACK(unit_goto_and_callback), paction);

#define ADD_OLD_ACCELERATOR(wanted_action_id, accel_key, accel_mods)      \
  if (act_id == wanted_action_id) {                                    \
    menu_unit_goto_and_add_accel(item, act_id, accel_key, accel_mods); \
  }

          /* Add the keyboard shortcuts for "Go to and..." menu items that
           * existed independently before the "Go to and..." menu arrived.
           */
          ADD_OLD_ACCELERATOR(ACTION_FOUND_CITY, GDK_KEY_b, GDK_SHIFT_MASK);
          ADD_OLD_ACCELERATOR(ACTION_JOIN_CITY, GDK_KEY_j, GDK_SHIFT_MASK);
          ADD_OLD_ACCELERATOR(ACTION_NUKE, GDK_KEY_n, GDK_SHIFT_MASK);
        }

        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_show(item);
      } action_noninternal_iterate_end;
    }
  }

  menu_entry_group_set_sensitive(MGROUP_SAFE, TRUE);
  menu_entry_group_set_sensitive(MGROUP_PLAYER, client_has_player());

  menu_entry_set_sensitive("TAX_RATE",
                           game.info.changable_tax
                           && can_client_issue_orders());
  menu_entry_set_sensitive("POLICIES",
                           multiplier_count() > 0);

  menu_entry_set_active("SHOW_CITY_OUTLINES",
                        gui_options.draw_city_outlines);
  menu_entry_set_active("SHOW_CITY_OUTPUT",
                        gui_options.draw_city_output);
  menu_entry_set_active("SHOW_MAP_GRID",
                        gui_options.draw_map_grid);
  menu_entry_set_active("SHOW_NATIONAL_BORDERS",
                        gui_options.draw_borders);
  menu_entry_set_sensitive("SHOW_NATIONAL_BORDERS",
                           BORDERS_DISABLED != game.info.borders);
  menu_entry_set_active("SHOW_NATIVE_TILES",
                        gui_options.draw_native);
  menu_entry_set_active("SHOW_CITY_FULL_BAR",
                        gui_options.draw_full_citybar);
  menu_entry_set_active("SHOW_CITY_NAMES",
                        gui_options.draw_city_names);
  menu_entry_set_active("SHOW_CITY_GROWTH",
                        gui_options.draw_city_growth);
  menu_entry_set_active("SHOW_CITY_PRODUCTIONS",
                        gui_options.draw_city_productions);
  menu_entry_set_active("SHOW_CITY_BUY_COST",
                        gui_options.draw_city_buycost);
  menu_entry_set_active("SHOW_CITY_TRADE_ROUTES",
                        gui_options.draw_city_trade_routes);
  menu_entry_set_active("SHOW_TERRAIN",
                        gui_options.draw_terrain);
  menu_entry_set_active("SHOW_COASTLINE",
                        gui_options.draw_coastline);
  menu_entry_set_active("SHOW_PATHS",
                        gui_options.draw_paths);
  menu_entry_set_active("SHOW_IRRIGATION",
                        gui_options.draw_irrigation);
  menu_entry_set_active("SHOW_MINES",
                        gui_options.draw_mines);
  menu_entry_set_active("SHOW_BASES",
                        gui_options.draw_fortress_airbase);
  menu_entry_set_active("SHOW_RESOURCES",
                        gui_options.draw_specials);
  menu_entry_set_active("SHOW_HUTS",
                        gui_options.draw_huts);
  menu_entry_set_active("SHOW_POLLUTION",
                        gui_options.draw_pollution);
  menu_entry_set_active("SHOW_CITIES",
                        gui_options.draw_cities);
  menu_entry_set_active("SHOW_UNITS",
                        gui_options.draw_units);
  menu_entry_set_active("SHOW_UNIT_SOLID_BG",
                        gui_options.solid_color_behind_units);
  menu_entry_set_active("SHOW_UNIT_SHIELDS",
                        gui_options.draw_unit_shields);
  menu_entry_set_active("SHOW_STACK_SIZE",
                        gui_options.draw_unit_stack_size);
  menu_entry_set_active("SHOW_FOCUS_UNIT",
                        gui_options.draw_focus_unit);
  menu_entry_set_active("SHOW_FOG_OF_WAR",
                        gui_options.draw_fog_of_war);

  view_menu_update_sensitivity();

  menu_entry_set_active("FULL_SCREEN", GUI_GTK_OPTION(fullscreen));
}

/**************************************************************************
  Free resources allocated by the menu system.
**************************************************************************/
void menus_free(void)
{
  if (ui_builder != NULL) {
    g_object_unref(ui_builder);
    ui_builder = NULL;
  }
}
