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

/* client/gui-gtk-4.0 */
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

static GMenu *main_menubar = NULL;
static bool menus_built = FALSE;

static GMenu *setup_menus(GtkApplication *app);

static void view_menu_update_sensitivity(GActionMap *map);

enum menu_entry_grouping { MGROUP_SAFE =    0B00000001,
                           MGROUP_EDIT =    0B00000010,
                           MGROUP_PLAYING = 0B00000100,
                           MGROUP_UNIT =    0B00001000,
                           MGROUP_PLAYER =  0B00010000,
                           MGROUP_CHAR =    0B00100000,
                           MGROUP_ALL =     0B11111111 };

static GMenu *options_menu = NULL;
static GMenu *edit_menu = NULL;
static GMenu *view_menu = NULL;
static GMenu *gov_menu = NULL;
static GMenu *unit_menu = NULL;
static GMenu *work_menu = NULL;
static GMenu *combat_menu = NULL;

struct menu_entry_info {
  const char *key;
  const char *name;
  const char *action;
  const char *accl;
  enum menu_entry_grouping grouping;
  void (*toggle)(GSimpleAction *act, GVariant *value, gpointer data);
  bool state; /* Only for toggle actions */
};

static void setup_app_actions(GApplication *fc_app);
static GMenuItem *create_toggle_menu_item(struct menu_entry_info *info);
static GMenuItem *create_toggle_menu_item_for_key(const char *key);

/* Menu entry callbacks */
static void clear_chat_logs_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data);
static void save_chat_logs_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data);
static void local_options_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data);
static void message_options_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data);
static void server_options_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data);
static void save_options_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void save_options_on_exit_callback(GSimpleAction *action,
                                          GVariant *parameter,
                                          gpointer data);
static void save_game_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);
static void save_game_as_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);

#ifdef FREECIV_DEBUG
static void reload_tileset_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data);
#endif /* FREECIV_DEBUG */

static void save_mapimg_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data);
static void save_mapimg_as_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data);
static void find_city_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);
static void worklists_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);
static void client_lua_script_callback(GSimpleAction *action,
                                       GVariant *parameter,
                                       gpointer data);
static void leave_callback(GSimpleAction *action,
                           GVariant *parameter,
                           gpointer data);
static void volume_up_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);
static void volume_down_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data);
static void quit_callback(GSimpleAction *action,
                          GVariant *parameter,
                          gpointer data);
static void map_view_callback(GSimpleAction *action,
                              GVariant *parameter,
                              gpointer data);
static void report_units_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void report_nations_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data);
static void report_cities_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data);
static void report_wow_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void report_top_cities_callback(GSimpleAction *action,
                                       GVariant *parameter,
                                       gpointer data);
static void report_messages_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data);
static void report_demographic_callback(GSimpleAction *action,
                                        GVariant *parameter,
                                        gpointer data);
static void help_overview_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data);
static void help_playing_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void help_policies_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data);
static void help_terrain_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void help_economy_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void help_cities_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data);
static void help_improvements_callback(GSimpleAction *action,
                                       GVariant *parameter,
                                       gpointer data);
static void help_wonders_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void help_units_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void help_combat_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data);
static void help_zoc_callback(GSimpleAction *action,
                              GVariant *parameter,
                              gpointer data);
static void help_government_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data);
static void help_diplomacy_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data);
static void help_tech_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);
static void help_space_race_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data);
static void help_ruleset_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void help_tileset_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void help_musicset_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data);
static void help_nations_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void help_connecting_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data);
static void help_controls_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data);
static void help_governor_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data);
static void help_chatline_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data);
static void help_worklist_editor_callback(GSimpleAction *action,
                                          GVariant *parameter,
                                          gpointer data);
static void help_language_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data);
static void help_copying_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void help_about_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void edit_mode_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);
static void show_city_outlines_callback(GSimpleAction *action,
                                        GVariant *parameter,
                                        gpointer data);
static void show_city_output_callback(GSimpleAction *action,
                                      GVariant *parameter,
                                      gpointer data);
static void show_map_grid_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data);
static void show_national_borders_callback(GSimpleAction *action,
                                           GVariant *parameter,
                                           gpointer data);
static void show_native_tiles_callback(GSimpleAction *action,
                                       GVariant *parameter,
                                       gpointer data);
static void show_city_full_bar_callback(GSimpleAction *action,
                                        GVariant *parameter,
                                        gpointer data);
static void show_city_names_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data);
static void show_city_growth_callback(GSimpleAction *action,
                                      GVariant *parameter,
                                      gpointer data);
static void show_city_productions_callback(GSimpleAction *action,
                                           GVariant *parameter,
                                           gpointer data);
static void show_city_buy_cost_callback(GSimpleAction *action,
                                        GVariant *parameter,
                                        gpointer data);
static void show_city_trade_routes_callback(GSimpleAction *action,
                                            GVariant *parameter,
                                            gpointer data);
static void show_terrain_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void show_coastline_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data);
static void show_paths_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void show_irrigation_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data);
static void show_mines_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void show_bases_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void show_resources_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data);
static void show_huts_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);
static void show_pollution_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data);
static void show_cities_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data);
static void show_units_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void show_unit_solid_bg_callback(GSimpleAction *action,
                                        GVariant *parameter,
                                        gpointer data);
static void show_unit_shields_callback(GSimpleAction *action,
                                       GVariant *parameter,
                                       gpointer data);
static void show_stack_size_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data);
static void show_focus_unit_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data);
static void show_fog_of_war_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data);
static void toggle_fog_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void scenario_properties_callback(GSimpleAction *action,
                                         GVariant *parameter,
                                         gpointer data);
static void save_scenario_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data);
static void full_screen_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data);
static void center_view_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data);
static void report_economy_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data);
static void report_research_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data);
static void multiplier_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void report_spaceship_callback(GSimpleAction *action,
                                      GVariant *parameter,
                                      gpointer data);
static void report_achievements_callback(GSimpleAction *action,
                                         GVariant *parameter,
                                         gpointer data);

static void government_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void revolution_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void tax_rate_callback(GSimpleAction *action,
                              GVariant *parameter,
                              gpointer data);
static void select_single_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data);
static void select_all_on_tile_callback(GSimpleAction *action,
                                        GVariant *parameter,
                                        gpointer data);
static void select_same_type_tile_callback(GSimpleAction *action,
                                           GVariant *parameter,
                                           gpointer data);
static void select_same_type_cont_callback(GSimpleAction *action,
                                           GVariant *parameter,
                                           gpointer data);
static void select_same_type_callback(GSimpleAction *action,
                                      GVariant *parameter,
                                      gpointer data);
static void select_dialog_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data);
static void rally_dialog_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void infra_dialog_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void unit_wait_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);
static void unit_done_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);
static void unit_goto_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);
static void unit_goto_city_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data);
static void unit_return_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data);
static void unit_explore_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void unit_patrol_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data);
static void unit_teleport_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data);
static void unit_sentry_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data);
static void fortify_callback(GSimpleAction *action,
                             GVariant *parameter,
                             gpointer data);
static void unit_homecity_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data);
static void unit_upgrade_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void unit_convert_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void unit_disband_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void unsentry_all_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void unit_unload_transporter_callback(GSimpleAction *action,
                                             GVariant *parameter,
                                             gpointer data);
static void unit_board_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void unit_deboard_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void build_city_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void auto_work_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);
static void cultivate_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);
static void plant_callback(GSimpleAction *action,
                           GVariant *parameter,
                           gpointer data);

static void paradrop_callback(GSimpleAction *action,
                              GVariant *parameter,
                              gpointer data);
static void pillage_callback(GSimpleAction *action,
                             GVariant *parameter,
                             gpointer data);
static void transform_terrain_callback(GSimpleAction *action,
                                       GVariant *parameter,
                                       gpointer data);
static void clean_callback(GSimpleAction *action,
                           GVariant *parameter,
                           gpointer data);
static void build_road_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void build_irrigation_callback(GSimpleAction *action,
                                      GVariant *parameter,
                                      gpointer data);
static void build_mine_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void connect_road_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void connect_rail_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void connect_maglev_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data);
static void connect_irrigation_callback(GSimpleAction *action,
                                        GVariant *parameter,
                                        gpointer data);
static void do_action_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);
static void build_fortress_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data);
static void build_airbase_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data);

static void bg_select_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);
static void bg_assign_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);
static void bg_append_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);

static struct menu_entry_info menu_entries[] =
{
  /* Game menu */
  { "CLEAR_CHAT_LOGS", N_("_Clear Chat Log"),
    "clear_chat", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "SAVE_CHAT_LOGS", N_("Save Chat _Log"),
    "save_chat", NULL, MGROUP_SAFE,
    NULL, FALSE },

  { "LOCAL_OPTIONS", N_("_Local Client"),
    "local_options", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "MESSAGE_OPTIONS", N_("_Message"),
    "message_options", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "SERVER_OPTIONS", N_("_Remote Server"),
    "server_options", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "SAVE_OPTIONS", N_("Save Options _Now"),
    "save_options", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "SAVE_OPTIONS_ON_EXIT", N_("Save Options on _Exit"),
    "save_options_on_exit", NULL, MGROUP_SAFE,
    save_options_on_exit_callback, FALSE },

#ifdef FREECIV_DEBUG
  { "RELOAD_TILESET", N_("_Reload Tileset"),
    "reload_tileset", ACCL_MOD_KEY"<alt>r", MGROUP_SAFE,
    NULL, FALSE },
#endif /* FREECIV_DEBUG */

  { "GAME_SAVE", N_("_Save Game"),
    "game_save", ACCL_MOD_KEY"s", MGROUP_SAFE,
    NULL, FALSE },
  { "GAME_SAVE_AS", N_("Save Game _As..."),
    "game_save_as", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "MAPIMG_SAVE", N_("Save Map _Image"),
    "save_mapimg", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "MAPIMG_SAVE_AS", N_("Save _Map Image As..."),
    "save_mapimg_as", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "VOLUME_UP", N_("Volume Up"),
    "volume_up", "greater", MGROUP_SAFE | MGROUP_CHAR,
    NULL, FALSE },
  { "VOLUME_DOWN", N_("Volume Down"),
    "volume_down", "less", MGROUP_SAFE | MGROUP_CHAR,
    NULL, FALSE },
  { "LEAVE", N_("_Leave"),
    "leave", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "QUIT", N_("_Quit"),
    "quit", ACCL_MOD_KEY"q", MGROUP_SAFE,
    NULL, FALSE },

  /* Edit menu */
  { "FIND_CITY", N_("_Find City"),
    "find_city", ACCL_MOD_KEY"f", MGROUP_SAFE,
    NULL, FALSE },
  { "WORKLISTS", N_("Work_lists"),
    "worklists", ACCL_MOD_KEY"<shift>l", MGROUP_SAFE,
    NULL, FALSE },
  { "RALLY_DLG", N_("Rally point dialog"),
    "rally_dlg", ACCL_MOD_KEY"<shift>r", MGROUP_SAFE,
    NULL, FALSE },
  { "INFRA_DLG", N_("Infra dialog"),
    "infra_dlg", ACCL_MOD_KEY"<shift>f", MGROUP_SAFE,
    NULL, FALSE },
  { "EDIT_MODE", N_("_Editing Mode"),
    "edit_mode", ACCL_MOD_KEY"e", MGROUP_SAFE,
    edit_mode_callback, FALSE },
  { "TOGGLE_FOG", N_("Toggle Fog of _War"),
    "toggle_fog", ACCL_MOD_KEY"<shift>w", MGROUP_EDIT,
    toggle_fog_callback, FALSE },
  { "SCENARIO_PROPERTIES", N_("Game/Scenario Properties"),
    "scenario_props", NULL, MGROUP_EDIT,
    NULL, FALSE },
  { "SCENARIO_SAVE", N_("Save Scenario"),
    "scenario_save", NULL, MGROUP_EDIT,
    NULL, FALSE },
  { "CLIENT_LUA_SCRIPT", N_("Client _Lua Script"),
    "lua_script", NULL, MGROUP_SAFE,
    NULL, FALSE },

  /* View menu */
  { "SHOW_CITY_OUTLINES", N_("Cit_y Outlines"),
    "show_city_outlines", ACCL_MOD_KEY"y", MGROUP_SAFE,
    show_city_outlines_callback, FALSE },
  { "SHOW_CITY_OUTPUT", N_("City Output"),
    "show_city_output", ACCL_MOD_KEY"v", MGROUP_SAFE,
    show_city_output_callback, FALSE },
  { "SHOW_MAP_GRID", N_("Map _Grid"),
    "show_map_grid", ACCL_MOD_KEY"g", MGROUP_SAFE,
    show_map_grid_callback, FALSE },
  { "SHOW_NAT_BORDERS", N_("National _Borders"),
    "show_nat_borders", ACCL_MOD_KEY"b", MGROUP_SAFE,
    show_national_borders_callback, FALSE },
  { "SHOW_NATIVE_TILES", N_("Native Tiles"),
    "show_native_tiles", ACCL_MOD_KEY"<shift>n", MGROUP_SAFE,
    show_native_tiles_callback, FALSE },
  { "SHOW_CITY_FULL_BAR", N_("City Full Bar"),
    "show_city_full_bar", NULL, MGROUP_SAFE,
    show_city_full_bar_callback, FALSE },
  { "SHOW_CITY_NAMES", N_("City _Names"),
    "show_city_names", ACCL_MOD_KEY"n", MGROUP_SAFE,
    show_city_names_callback, FALSE },
  { "SHOW_CITY_GROWTH", N_("City G_rowth"),
    "show_city_growth", ACCL_MOD_KEY"o", MGROUP_SAFE,
    show_city_growth_callback, FALSE },
  { "SHOW_CITY_PRODUCTIONS", N_("City _Production"),
    "show_city_productions", ACCL_MOD_KEY"p", MGROUP_SAFE,
    show_city_productions_callback, FALSE },
  { "SHOW_CITY_BUY_COST", N_("City Buy Cost"),
    "show_city_buy_cost", NULL, MGROUP_SAFE,
    show_city_buy_cost_callback, FALSE },
  { "SHOW_CITY_TRADE_ROUTES", N_("City Tra_deroutes"),
    "show_trade_routes", ACCL_MOD_KEY"d", MGROUP_SAFE,
    show_city_trade_routes_callback, FALSE },
  { "SHOW_TERRAIN", N_("_Terrain"),
    "show_terrain", NULL, MGROUP_SAFE,
    show_terrain_callback, FALSE },
  { "SHOW_COASTLINE", N_("C_oastline"),
    "show_coastline", NULL, MGROUP_SAFE,
    show_coastline_callback, FALSE },
  { "SHOW_PATHS", N_("_Paths"),
    "show_paths", NULL, MGROUP_SAFE,
    show_paths_callback, FALSE },
  { "SHOW_IRRIGATION", N_("_Irrigation"),
    "show_irrigation", NULL, MGROUP_SAFE,
    show_irrigation_callback, FALSE },
  { "SHOW_MINES", N_("_Mines"),
    "show_mines", NULL, MGROUP_SAFE,
    show_mines_callback, FALSE },
  { "SHOW_BASES", N_("_Bases"),
    "show_bases", NULL, MGROUP_SAFE,
    show_bases_callback, FALSE },
  { "SHOW_RESOURCES", N_("_Resources"),
    "show_resources", NULL, MGROUP_SAFE,
    show_resources_callback, FALSE },
  { "SHOW_HUTS", N_("_Huts"),
    "show_huts", NULL, MGROUP_SAFE,
    show_huts_callback, FALSE },
  { "SHOW_POLLUTION", N_("Po_llution & Fallout"),
    "show_pollution", NULL, MGROUP_SAFE,
    show_pollution_callback, FALSE },
  { "SHOW_CITIES", N_("Citi_es"),
    "show_cities", NULL, MGROUP_SAFE,
    show_cities_callback, FALSE },
  { "SHOW_UNITS", N_("_Units"),
    "show_units", NULL, MGROUP_SAFE,
    show_units_callback, FALSE },
  { "SHOW_UNIT_SOLID_BG", N_("Unit Solid Background"),
    "show_unit_solid_bg", NULL, MGROUP_SAFE,
    show_unit_solid_bg_callback, FALSE },
  { "SHOW_UNIT_SHIELDS", N_("Unit shields"),
    "show_unit_shields", NULL, MGROUP_SAFE,
    show_unit_shields_callback, FALSE },
  { "SHOW_STACK_SIZE", N_("Unit Stack Size"),
    "show_stack_size", ACCL_MOD_KEY"plus", MGROUP_SAFE,
    show_stack_size_callback, FALSE },
  { "SHOW_FOCUS_UNIT", N_("Focu_s Unit"),
    "show_focus_unit", NULL, MGROUP_SAFE,
    show_focus_unit_callback, FALSE },
  { "SHOW_FOG_OF_WAR", N_("Fog of _War"),
    "show_fow", NULL, MGROUP_SAFE,
    show_fog_of_war_callback, FALSE },

  { "FULL_SCREEN", N_("_Fullscreen"),
    "full_screen", ACCL_MOD_KEY"F11", MGROUP_SAFE,
    full_screen_callback, FALSE },
  { "CENTER_VIEW", N_("_Center View"),
    "center_view", "c", MGROUP_PLAYER | MGROUP_CHAR,
    NULL, FALSE },

  /* Select menu */
  { "SELECT_SINGLE", N_("_Single Unit (Unselect Others)"),
    "select_single", "z", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "SELECT_ALL_ON_TILE", N_("_All On Tile"),
    "select_all_tile", "v", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "SELECT_SAME_TYPE_TILE", N_("Same Type on _Tile"),
    "select_same_type_tile", "<shift>v", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "SELECT_SAME_TYPE_CONT", N_("Same Type on _Continent"),
    "select_same_type_cont", "<shift>c", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "SELECT_SAME_TYPE", N_("Same Type _Everywhere"),
    "select_same_type", "<shift>x", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "SELECT_DLG", N_("Unit Selection Dialog"),
    "select_dlg", NULL, MGROUP_UNIT,
    NULL, FALSE },

  /* Unit menu */
  { "UNIT_GOTO", N_("_Go to"),
    "goto", "g", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "UNIT_GOTO_CITY", N_("Go _to/Airlift to City..."),
    "goto_city", "t", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "UNIT_RETURN", N_("_Return to Nearest City"),
    "return", "<shift>g", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "UNIT_EXPLORE", N_("Auto E_xplore"),
    "explore", "x", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "UNIT_PATROL", N_("_Patrol"),
    "patrol", "q", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "UNIT_TELEPORT", N_("_Teleport"),
    "teleport", NULL, MGROUP_UNIT,
    NULL, FALSE },
  { "UNIT_SENTRY", N_("_Sentry"),
    "sentry", "s", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "UNSENTRY_ALL", N_("Uns_entry All On Tile"),
    "unsentry_all", "<shift>s", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "UNIT_BOARD", N_("_Load"),
    "board", "l", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "UNIT_DEBOARD", N_("_Unload"),
    "deboard", "u", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "UNIT_UNLOAD_TRANSPORTER", N_("U_nload All From Transporter"),
    "unload_transporter", "<shift>t", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "UNIT_HOMECITY", N_("Set _Home City"),
    "homecity", "h", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "UNIT_UPGRADE", N_("Upgr_ade"),
    "upgrade", "<shift>u", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "UNIT_CONVERT", N_("C_onvert"),
    "convert", "<shift>o", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "UNIT_DISBAND", N_("_Disband"),
    "disband", "<shift>d", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "DO_ACTION", N_("_Do..."),
    "do_action", "d", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "UNIT_WAIT", N_("_Wait"),
    "wait", "w", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "UNIT_DONE", N_("_Done"),
    "done", "space", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },

  /* Work menu */
  { "BUILD_CITY", N_("_Build City"),
    "build_city", "b", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "AUTO_WORKER", N_("_Auto Worker"),
    "auto_work", "a", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "BUILD_ROAD", N_("Build _Road"),
    "build_road", "r", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "BUILD_IRRIGATION", N_("Build _Irrigation"),
    "build_irrigation", "i", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "BUILD_MINE", N_("Build _Mine"),
    "build_mine", "m", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "CULTIVATE", N_("Cultivate"),
    "cultivate", "<shift>i", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "PLANT", N_("Plant"),
    "plant", "<shift>m", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "TRANSFORM_TERRAIN", N_("Transf_orm Terrain"),
    "transform_terrain", "o", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "CONNECT_ROAD", N_("Connect With Roa_d"),
    "connect_road", ACCL_MOD_KEY"r", MGROUP_UNIT,
    NULL, FALSE },
  { "CONNECT_RAIL", N_("Connect With Rai_l"),
    "connect_rail", ACCL_MOD_KEY"l", MGROUP_UNIT,
    NULL, FALSE },
  { "CONNECT_MAGLEV", N_("Connect With _Maglev"),
    "connect_maglev", ACCL_MOD_KEY"m", MGROUP_UNIT,
    NULL, FALSE },
  { "CONNECT_IRRIGATION", N_("Connect With Irri_gation"),
    "connect_irrigation", ACCL_MOD_KEY"i", MGROUP_UNIT,
    NULL, FALSE },
  { "CLEAN", N_("_Clean"),
    "clean", "p", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },

  /* Combat menu */
  { "FORTIFY", N_("Fortify"),
    "fortify", "f", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "BUILD_FORTRESS", N_("Build Fortress"),
    "build_base_fortress", "<shift>f", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "BUILD_AIRBASE", N_("Build Airbase"),
    "build_base_airbase", "<shift>e", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "PARADROP", N_("P_aradrop"),
    "paradrop", "j", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },
  { "PILLAGE", N_("_Pillage"),
    "pillage", "<shift>p", MGROUP_UNIT | MGROUP_CHAR,
    NULL, FALSE },

  /* Civilization */
  { "MAP_VIEW", N_("?noun:_View"),
    "map_view", "F1", MGROUP_SAFE,
    NULL, FALSE },
  { "REPORT_UNITS", N_("_Units"),
    "report_units", "F2", MGROUP_SAFE,
    NULL, FALSE },
  { "REPORT_NATIONS", N_("_Nations"),
    "report_nations", "F3", MGROUP_SAFE,
    NULL, FALSE },
  { "REPORT_CITIES", N_("_Cities"),
    "report_cities", "F4", MGROUP_SAFE,
    NULL, FALSE },
  { "REPORT_ECONOMY", N_("_Economy"),
    "report_economy", "F5", MGROUP_PLAYER,
    NULL, FALSE },
  { "REPORT_RESEARCH", N_("_Research"),
    "report_research", "F6", MGROUP_PLAYER,
    NULL, FALSE },

  { "TAX_RATES", N_("_Tax Rates..."),
    "tax_rates", ACCL_MOD_KEY"t", MGROUP_PLAYING,
    NULL, FALSE },
  { "POLICIES", N_("_Policies..."),
    "policies", "<shift><ctrl>p", MGROUP_PLAYER,
    NULL, FALSE },
  { "START_REVOLUTION", N_("_Revolution..."),
    "revolution", "<shift><ctrl>g", MGROUP_PLAYING,
    NULL, FALSE },

  { "REPORT_WOW", N_("_Wonders of the World"),
    "report_wow", "F7", MGROUP_SAFE,
    NULL, FALSE },
  { "REPORT_TOP_CITIES", N_("Top Cities"),
    "report_top_cities", "F8", MGROUP_SAFE,
    NULL, FALSE },
  { "REPORT_MESSAGES", N_("_Messages"),
    "report_messages", "F9", MGROUP_SAFE,
    NULL, FALSE },
  { "REPORT_DEMOGRAPHIC", N_("_Demographics"),
    "report_demographics", "F11", MGROUP_SAFE,
    NULL, FALSE },
  { "REPORT_SPACESHIP", N_("_Spaceship"),
    "report_spaceship", "F12", MGROUP_SAFE,
    NULL, FALSE },
  { "REPORT_ACHIEVEMENTS", N_("_Achievements"),
    "report_achievements", "asterisk", MGROUP_SAFE | MGROUP_CHAR,
    NULL, FALSE },

  /* Battle Groups menu */
  /* Note that user view: 1 - 4, internal: 0 - 3 */
  { "BG_SELECT_1", N_("Select Battle Group 1"),
    "bg_select_0", "<shift>F1", MGROUP_PLAYING,
    NULL, FALSE },
  { "BG_ASSIGN_1", N_("Assign Battle Group 1"),
    "bg_assign_0", ACCL_MOD_KEY"F1", MGROUP_PLAYING,
    NULL, FALSE },
  { "BG_APPEND_1", N_("Append to Battle Group 1"),
    "bg_append_0", "<shift><ctrl>F1", MGROUP_PLAYING,
    NULL, FALSE },
  { "BG_SELECT_2", N_("Select Battle Group 2"),
    "bg_select_1", "<shift>F2", MGROUP_PLAYING,
    NULL, FALSE },
  { "BG_ASSIGN_2", N_("Assign Battle Group 2"),
    "bg_assign_1", ACCL_MOD_KEY"F2", MGROUP_PLAYING,
    NULL, FALSE },
  { "BG_APPEND_2", N_("Append to Battle Group 2"),
    "bg_append_1", "<shift><ctrl>F2", MGROUP_PLAYING,
    NULL, FALSE },
  { "BG_SELECT_3", N_("Select Battle Group 3"),
    "bg_select_2", "<shift>F3", MGROUP_PLAYING,
    NULL, FALSE },
  { "BG_ASSIGN_3", N_("Assign Battle Group 3"),
    "bg_assign_2", ACCL_MOD_KEY"F3", MGROUP_PLAYING,
    NULL, FALSE },
  { "BG_APPEND_3", N_("Append to Battle Group 3"),
    "bg_append_2", "<shift><ctrl>F3", MGROUP_PLAYING,
    NULL, FALSE },
  { "BG_SELECT_4", N_("Select Battle Group 4"),
    "bg_select_3", "<shift>F4", MGROUP_PLAYING,
    NULL, FALSE },
  { "BG_ASSIGN_4", N_("Assign Battle Group 4"),
    "bg_assign_3", ACCL_MOD_KEY"F4", MGROUP_PLAYING,
    NULL, FALSE },
  { "BG_APPEND_4", N_("Append to Battle Group 4"),
    "bg_append_3", "<shift><ctrl>F4", MGROUP_PLAYING,
    NULL, FALSE },

  /* Help menu */
  { "HELP_OVERVIEW", N_("?help:Overview"),
    "help_overview", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_PLAYING", N_("Strategy and Tactics"),
    "help_playing", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_POLICIES", N_("Policies"),
    "help_policies", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_TERRAIN", N_("Terrain"),
    "help_terrains", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_ECONOMY", N_("Economy"),
    "help_economy", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_CITIES", N_("Cities"),
    "help_cities", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_IMPROVEMENTS", N_("City Improvements"),
    "help_improvements", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_WONDERS", N_("Wonders of the World"),
    "help_wonders", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_UNITS", N_("Units"),
    "help_units", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_COMBAT", N_("Combat"),
    "help_combat", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_ZOC", N_("Zones of Control"),
    "help_zoc", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_GOVERNMENT", N_("Government"),
    "help_government", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_DIPLOMACY", N_("Diplomacy"),
    "help_diplomacy", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_TECH", N_("Technology"),
    "help_tech", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_SPACE_RACE", N_("Space Race"),
    "help_space_race", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_RULESET", N_("About Current Ruleset"),
    "help_ruleset", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_TILESET", N_("About Current Tileset"),
    "help_tileset", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_MUSICSET", N_("About Current Musicset"),
    "help_musicset", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_NATIONS", N_("About Nations"),
    "help_nations", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_CONNECTING", N_("Connecting"),
    "help_connecting", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_CONTROLS", N_("Controls"),
    "help_controls", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_GOVERNOR", N_("Citizen Governor"),
    "help_governor", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_CHATLINE", N_("Chatline"),
    "help_chatline", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_WORKLIST_EDITOR", N_("Worklist Editor"),
    "help_worklist_editor", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_LANGUAGES", N_("Languages"),
    "help_languages", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_COPYING", N_("Copying"),
    "help_copying", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { "HELP_ABOUT", N_("About Freeciv"),
    "help_about", NULL, MGROUP_SAFE,
    NULL, FALSE },
  { NULL }
};

enum {
  VMENU_CITY_OUTLINES = 0,
  VMENU_CITY_OUTPUT,
  VMENU_MAP_GRID,
  VMENU_NAT_BORDERS,
  VMENU_NATIVE_TILES,
  VMENU_CITY_FULL_BAR,
  VMENU_CITY_NAMES,
  VMENU_CITY_GROWTH,
  VMENU_CITY_PRODUCTIONS,
  VMENU_CITY_BUY_COST,
  VMENU_CITY_TRADE_ROUTES,
  VMENU_TERRAIN,
  VMENU_COASTLINE,
  VMENU_PATHS,
  VMENU_IRRIGATION,
  VMENU_MINES,
  VMENU_BASES,
  VMENU_RESOURCES,
  VMENU_HUTS,
  VMENU_POLLUTION,
  VMENU_CITIES,
  VMENU_UNITS,
  VMENU_UNIT_SOLID_BG,
  VMENU_UNIT_SHIELDS,
  VMENU_STACK_SIZE,
  VMENU_FOCUS_UNIT,
  VMENU_FOW,
  VMENU_FULL_SCREEN
};

const GActionEntry acts[] = {
  { "clear_chat", clear_chat_logs_callback },
  { "save_chat", save_chat_logs_callback },
  { "local_options", local_options_callback },
  { "message_options", message_options_callback },
  { "server_options", server_options_callback },
  { "save_options", save_options_callback },

#ifdef FREECIV_DEBUG
  { "reload_tileset", reload_tileset_callback },
#endif /* FREECIV_DEBUG */

  { "game_save", save_game_callback },
  { "game_save_as", save_game_as_callback },
  { "save_mapimg", save_mapimg_callback },
  { "save_mapimg_as", save_mapimg_as_callback },
  { "volume_up", volume_up_callback },
  { "volume_down", volume_down_callback },
  { "leave", leave_callback },
  { "quit", quit_callback },

  { "find_city", find_city_callback },
  { "worklists", worklists_callback },
  { "rally_dlg", rally_dialog_callback },
  { "infra_dlg", infra_dialog_callback },
  { "scenario_props", scenario_properties_callback },
  { "scenario_save", save_scenario_callback },
  { "lua_script", client_lua_script_callback },

  { "center_view", center_view_callback },

  { "select_single", select_single_callback },
  { "select_all_tile", select_all_on_tile_callback },
  { "select_same_type_tile", select_same_type_tile_callback },
  { "select_same_type_cont", select_same_type_cont_callback },
  { "select_same_type", select_same_type_callback },
  { "select_dlg", select_dialog_callback },

  { "goto", unit_goto_callback },
  { "goto_city", unit_goto_city_callback },
  { "return", unit_return_callback },
  { "explore", unit_explore_callback },
  { "patrol", unit_patrol_callback },
  { "teleport", unit_teleport_callback },
  { "sentry", unit_sentry_callback },
  { "unsentry_all", unsentry_all_callback },
  { "board", unit_board_callback },
  { "deboard", unit_deboard_callback },
  { "unload_transporter", unit_unload_transporter_callback },
  { "homecity", unit_homecity_callback },
  { "upgrade", unit_upgrade_callback },
  { "convert", unit_convert_callback },
  { "disband", unit_disband_callback },
  { "do_action", do_action_callback },
  { "wait", unit_wait_callback },
  { "done", unit_done_callback },

  { "build_city", build_city_callback },
  { "auto_work", auto_work_callback },
  { "build_road", build_road_callback },
  { "build_irrigation", build_irrigation_callback },
  { "build_mine", build_mine_callback },
  { "cultivate", cultivate_callback },
  { "plant", plant_callback },
  { "transform_terrain", transform_terrain_callback },
  { "connect_road", connect_road_callback },
  { "connect_rail", connect_rail_callback },
  { "connect_maglev", connect_maglev_callback },
  { "connect_irrigation", connect_irrigation_callback },
  { "clean", clean_callback },

  { "fortify", fortify_callback },
  { "build_base_fortress", build_fortress_callback },
  { "build_base_airbase", build_airbase_callback },
  { "paradrop", paradrop_callback },
  { "pillage", pillage_callback },

  { "revolution", revolution_callback },

  { "map_view", map_view_callback },
  { "report_units", report_units_callback },
  { "report_nations", report_nations_callback },
  { "report_cities", report_cities_callback },
  { "report_economy", report_economy_callback },
  { "report_research", report_research_callback },
  { "tax_rates", tax_rate_callback },
  { "policies", multiplier_callback },
  { "report_wow", report_wow_callback },
  { "report_top_cities", report_top_cities_callback },
  { "report_messages", report_messages_callback },
  { "report_demographics", report_demographic_callback },
  { "report_spaceship", report_spaceship_callback },
  { "report_achievements", report_achievements_callback },

  { "help_overview", help_overview_callback },
  { "help_playing", help_playing_callback },
  { "help_policies", help_policies_callback },
  { "help_terrains", help_terrain_callback },
  { "help_economy", help_economy_callback },
  { "help_cities", help_cities_callback },
  { "help_improvements", help_improvements_callback },
  { "help_wonders", help_wonders_callback },
  { "help_units", help_units_callback },
  { "help_combat", help_combat_callback },
  { "help_zoc", help_zoc_callback },
  { "help_government", help_government_callback },
  { "help_diplomacy", help_diplomacy_callback },
  { "help_tech", help_tech_callback },
  { "help_space_race", help_space_race_callback },
  { "help_ruleset", help_ruleset_callback },
  { "help_tileset", help_tileset_callback },
  { "help_musicset", help_musicset_callback },
  { "help_nations", help_nations_callback },
  { "help_connecting", help_connecting_callback },
  { "help_controls", help_controls_callback },
  { "help_governor", help_governor_callback },
  { "help_chatline", help_chatline_callback },
  { "help_worklist_editor", help_worklist_editor_callback },
  { "help_languages", help_language_callback },
  { "help_copying", help_copying_callback },
  { "help_about", help_about_callback }
};

static struct menu_entry_info *menu_entry_info_find(const char *key);

/************************************************************************//**
  Item "CLEAR_CHAT_LOGS" callback.
****************************************************************************/
static void clear_chat_logs_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data)
{
  clear_output_window();
}

/************************************************************************//**
  Item "SAVE_CHAT_LOGS" callback.
****************************************************************************/
static void save_chat_logs_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data)
{
  log_output_window();
}

/************************************************************************//**
  Item "LOCAL_OPTIONS" callback.
****************************************************************************/
static void local_options_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data)
{
  option_dialog_popup(_("Set local options"), client_optset);
}

/************************************************************************//**
  Item "MESSAGE_OPTIONS" callback.
****************************************************************************/
static void message_options_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data)
{
  popup_messageopt_dialog();
}

/************************************************************************//**
  Item "SERVER_OPTIONS" callback.
****************************************************************************/
static void server_options_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data)
{
  option_dialog_popup(_("Game Settings"), server_optset);
}

/************************************************************************//**
  Item "SAVE_OPTIONS" callback.
****************************************************************************/
static void save_options_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  options_save(NULL);
}

/************************************************************************//**
  Item "SAVE_OPTIONS_ON_EXIT" callback.
****************************************************************************/
static void save_options_on_exit_callback(GSimpleAction *action,
                                          GVariant *parameter,
                                          gpointer data)
{
  struct menu_entry_info *info = (struct menu_entry_info *)data;

  info->state ^= 1;
  gui_options.save_options_on_exit = info->state;

  g_menu_remove(options_menu, 4);

  menu_item_insert_unref(options_menu, 4,
                         create_toggle_menu_item(info));
}

#ifdef FREECIV_DEBUG
/************************************************************************//**
  Item "RELOAD_TILESET" callback.
****************************************************************************/
static void reload_tileset_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data)
{
  tilespec_reread(NULL, TRUE, 1.0);
}
#endif /* FREECIV_DEBUG */

/************************************************************************//**
  Item "SAVE_GAME" callback.
****************************************************************************/
static void save_game_callback(GSimpleAction *action, GVariant *parameter,
                               gpointer data)
{
  send_save_game(NULL);
}

/************************************************************************//**
  Item "SAVE_GAME_AS" callback.
****************************************************************************/
static void save_game_as_callback(GSimpleAction *action, GVariant *parameter,
                                  gpointer data)
{
  save_game_dialog_popup();
}

/************************************************************************//**
  Item "SAVE_MAPIMG" callback.
****************************************************************************/
static void save_mapimg_callback(GSimpleAction *action, GVariant *parameter,
                                 gpointer data)
{
  mapimg_client_save(NULL);
}

/************************************************************************//**
  Item "SAVE_MAPIMG_AS" callback.
****************************************************************************/
static void save_mapimg_as_callback(GSimpleAction *action, GVariant *parameter,
                                    gpointer data)
{
  save_mapimg_dialog_popup();
}

/************************************************************************//**
  This is the response callback for the dialog with the message:
  Leaving a local game will end it!
****************************************************************************/
static void leave_local_game_response(GtkWidget *dialog, gint response)
{
  gtk_window_destroy(GTK_WINDOW(dialog));
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
static void leave_callback(GSimpleAction *action,
                           GVariant *parameter,
                           gpointer data)
{
  if (is_server_running()) {
    GtkWidget *dialog
      = gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_WARNING,
                               GTK_BUTTONS_OK_CANCEL,
                               _("Leaving a local game will end it!"));
    setup_dialog(dialog, toplevel);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(leave_local_game_response), NULL);
    gtk_window_present(GTK_WINDOW(dialog));
  } else {
    disconnect_from_server(TRUE);
  }
}

/************************************************************************//**
  Item "VOLUME_UP" callback.
****************************************************************************/
static void volume_up_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data)
{
  struct option *poption = optset_option_by_name(client_optset, "sound_effects_volume");

  gui_options.sound_effects_volume += 10;
  gui_options.sound_effects_volume = CLIP(0, gui_options.sound_effects_volume, 100);
  option_changed(poption);
}

/************************************************************************//**
  Item "VOLUME_DOWN" callback.
****************************************************************************/
static void volume_down_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data)
{
  struct option *poption = optset_option_by_name(client_optset, "sound_effects_volume");

  gui_options.sound_effects_volume -= 10;
  gui_options.sound_effects_volume = CLIP(0, gui_options.sound_effects_volume, 100);
  option_changed(poption);
}

/************************************************************************//**
  Item "QUIT" callback.
****************************************************************************/
static void quit_callback(GSimpleAction *action,
                          GVariant *parameter,
                          gpointer data)
{
  popup_quit_dialog();
}

/************************************************************************//**
  Item "FIND_CITY" callback.
****************************************************************************/
static void find_city_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data)
{
  popup_find_dialog();
}

/************************************************************************//**
  Item "WORKLISTS" callback.
****************************************************************************/
static void worklists_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data)
{
  popup_worklists_report();
}

/************************************************************************//**
  Item "MAP_VIEW" callback.
****************************************************************************/
static void map_view_callback(GSimpleAction *action,
                              GVariant *parameter, gpointer data)
{
  map_canvas_focus();
}

/************************************************************************//**
  Item "REPORT_NATIONS" callback.
****************************************************************************/
static void report_nations_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data)
{
  popup_players_dialog(TRUE);
}

/************************************************************************//**
  Item "REPORT_WOW" callback.
****************************************************************************/
static void report_wow_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data)
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
static void report_top_cities_callback(GSimpleAction *action,
                                       GVariant *parameter,
                                       gpointer data)
{
  send_report_request(REPORT_TOP_CITIES);
}

/************************************************************************//**
  Item "REPORT_MESSAGES" callback.
****************************************************************************/
static void report_messages_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data)
{
  meswin_dialog_popup(TRUE);
}

/************************************************************************//**
  Item "CLIENT_LUA_SCRIPT" callback.
****************************************************************************/
static void client_lua_script_callback(GSimpleAction *action,
                                       GVariant *parameter,
                                       gpointer data)
{
  luaconsole_dialog_popup(TRUE);
}

/************************************************************************//**
  Item "REPORT_DEMOGRAPHIC" callback.
****************************************************************************/
static void report_demographic_callback(GSimpleAction *action,
                                        GVariant *parameter,
                                        gpointer data)
{
  send_report_request(REPORT_DEMOGRAPHIC);
}

/************************************************************************//**
  Item "REPORT_ACHIEVEMENTS" callback.
****************************************************************************/
static void report_achievements_callback(GSimpleAction *action,
                                         GVariant *parameter,
                                         gpointer data)
{
  send_report_request(REPORT_ACHIEVEMENTS);
}

/************************************************************************//**
  Item "HELP_OVERVIEW" callback.
****************************************************************************/
static void help_overview_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data)
{
  popup_help_dialog_string(HELP_OVERVIEW_ITEM);
}

/************************************************************************//**
  Item "HELP_PLAYING" callback.
****************************************************************************/
static void help_playing_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  popup_help_dialog_string(HELP_PLAYING_ITEM);
}

/************************************************************************//**
  Item "HELP_POLICIES" callback.
****************************************************************************/
static void help_policies_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data)
{
  popup_help_dialog_string(HELP_MULTIPLIER_ITEM);
}

/************************************************************************//**
  Item "HELP_TERRAIN" callback.
****************************************************************************/
static void help_terrain_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  popup_help_dialog_string(HELP_TERRAIN_ITEM);
}

/************************************************************************//**
  Item "HELP_ECONOMY" callback.
****************************************************************************/
static void help_economy_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  popup_help_dialog_string(HELP_ECONOMY_ITEM);
}

/************************************************************************//**
  Item "HELP_CITIES" callback.
****************************************************************************/
static void help_cities_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data)
{
  popup_help_dialog_string(HELP_CITIES_ITEM);
}

/************************************************************************//**
  Item "HELP_IMPROVEMENTS" callback.
****************************************************************************/
static void help_improvements_callback(GSimpleAction *action,
                                       GVariant *parameter,
                                       gpointer data)
{
  popup_help_dialog_string(HELP_IMPROVEMENTS_ITEM);
}

/************************************************************************//**
  Item "HELP_WONDERS" callback.
****************************************************************************/
static void help_wonders_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  popup_help_dialog_string(HELP_WONDERS_ITEM);
}

/************************************************************************//**
  Item "HELP_UNITS" callback.
****************************************************************************/
static void help_units_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data)
{
  popup_help_dialog_string(HELP_UNITS_ITEM);
}

/************************************************************************//**
  Item "HELP_COMBAT" callback.
****************************************************************************/
static void help_combat_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data)
{
  popup_help_dialog_string(HELP_COMBAT_ITEM);
}

/************************************************************************//**
  Item "HELP_ZOC" callback.
****************************************************************************/
static void help_zoc_callback(GSimpleAction *action,
                              GVariant *parameter,
                              gpointer data)
{
  popup_help_dialog_string(HELP_ZOC_ITEM);
}

/************************************************************************//**
  Item "HELP_GOVERNMENT" callback.
****************************************************************************/
static void help_government_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data)
{
  popup_help_dialog_string(HELP_GOVERNMENT_ITEM);
}

/************************************************************************//**
  Item "HELP_DIPLOMACY" callback.
****************************************************************************/
static void help_diplomacy_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data)
{
  popup_help_dialog_string(HELP_DIPLOMACY_ITEM);
}

/************************************************************************//**
  Item "HELP_TECH" callback.
****************************************************************************/
static void help_tech_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data)
{
  popup_help_dialog_string(HELP_TECHS_ITEM);
}

/************************************************************************//**
  Item "HELP_SPACE_RACE" callback.
****************************************************************************/
static void help_space_race_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data)
{
  popup_help_dialog_string(HELP_SPACE_RACE_ITEM);
}

/************************************************************************//**
  Item "HELP_RULESET" callback.
****************************************************************************/
static void help_ruleset_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  popup_help_dialog_string(HELP_RULESET_ITEM);
}

/************************************************************************//**
  Item "HELP_TILESET" callback.
****************************************************************************/
static void help_tileset_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  popup_help_dialog_string(HELP_TILESET_ITEM);
}

/************************************************************************//**
  Item "HELP_MUSICSET" callback.
****************************************************************************/
static void help_musicset_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data)
{
  popup_help_dialog_string(HELP_MUSICSET_ITEM);
}

/************************************************************************//**
  Item "HELP_NATIONS" callback.
****************************************************************************/
static void help_nations_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  popup_help_dialog_string(HELP_NATIONS_ITEM);
}

/************************************************************************//**
  Item "HELP_CONNECTING" callback.
****************************************************************************/
static void help_connecting_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data)
{
  popup_help_dialog_string(HELP_CONNECTING_ITEM);
}

/************************************************************************//**
  Item "HELP_CONTROLS" callback.
****************************************************************************/
static void help_controls_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data)
{
  popup_help_dialog_string(HELP_CONTROLS_ITEM);
}

/************************************************************************//**
  Item "HELP_GOVERNOR" callback.
****************************************************************************/
static void help_governor_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data)
{
  popup_help_dialog_string(HELP_CMA_ITEM);
}

/************************************************************************//**
  Item "HELP_CHATLINE" callback.
****************************************************************************/
static void help_chatline_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data)
{
  popup_help_dialog_string(HELP_CHATLINE_ITEM);
}

/************************************************************************//**
  Item "HELP_WORKLIST_EDITOR" callback.
****************************************************************************/
static void help_worklist_editor_callback(GSimpleAction *action,
                                          GVariant *parameter,
                                          gpointer data)
{
  popup_help_dialog_string(HELP_WORKLIST_EDITOR_ITEM);
}

/************************************************************************//**
  Item "HELP_LANGUAGE" callback.
****************************************************************************/
static void help_language_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data)
{
  popup_help_dialog_string(HELP_LANGUAGES_ITEM);
}

/************************************************************************//**
  Item "HELP_COPYING" callback.
****************************************************************************/
static void help_copying_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  popup_help_dialog_string(HELP_COPYING_ITEM);
}

/************************************************************************//**
  Item "HELP_ABOUT" callback.
****************************************************************************/
static void help_about_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data)
{
  popup_help_dialog_string(HELP_ABOUT_ITEM);
}

/************************************************************************//**
  Item "EDIT_MODE" callback.
****************************************************************************/
static void edit_mode_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data)
{
  key_editor_toggle();

  /* Unreachable techs in reqtree on/off */
  science_report_dialog_popdown();
}

struct menu_entry_option_map {
  const char *menu_entry;
  bool *option;
  int menu_pos;
};

const struct menu_entry_option_map meoms[] = {
  { "SAVE_OPTIONS_ON_EXIT", &gui_options.save_options_on_exit, -1 },
  { "FULL_SCREEN", &(GUI_GTK_OPTION(fullscreen)), -1 },
  { "EDIT_MODE", &game.info.is_edit_mode, -1 },
  { "TOGGLE_FOG", &game.client.fog_of_war, -1 },
  { "SHOW_CITY_OUTLINES", &gui_options.draw_city_outlines, VMENU_CITY_OUTLINES },
  { "SHOW_CITY_OUTPUT", &gui_options.draw_city_output, VMENU_CITY_OUTPUT },
  { "SHOW_MAP_GRID", &gui_options.draw_map_grid, VMENU_MAP_GRID },
  { "SHOW_NAT_BORDERS", &gui_options.draw_borders, VMENU_NAT_BORDERS },
  { "SHOW_NATIVE_TILES", &gui_options.draw_native, VMENU_NATIVE_TILES },
  { "SHOW_CITY_FULL_BAR", &gui_options.draw_full_citybar, VMENU_CITY_FULL_BAR },
  { "SHOW_CITY_NAMES", &gui_options.draw_city_names, VMENU_CITY_NAMES },
  { "SHOW_CITY_GROWTH", &gui_options.draw_city_growth, VMENU_CITY_GROWTH },
  { "SHOW_CITY_PRODUCTIONS", &gui_options.draw_city_productions,
    VMENU_CITY_PRODUCTIONS },
  { "SHOW_CITY_BUY_COST", &gui_options.draw_city_buycost, VMENU_CITY_BUY_COST },
  { "SHOW_CITY_TRADE_ROUTES", &gui_options.draw_city_trade_routes,
    VMENU_CITY_TRADE_ROUTES },
  { "SHOW_TERRAIN", &gui_options.draw_terrain, VMENU_TERRAIN },
  { "SHOW_COASTLINE", &gui_options.draw_coastline, VMENU_COASTLINE },
  { "SHOW_PATHS", &gui_options.draw_paths, VMENU_PATHS },
  { "SHOW_IRRIGATION", &gui_options.draw_irrigation, VMENU_IRRIGATION },
  { "SHOW_MINES", &gui_options.draw_mines, VMENU_MINES },
  { "SHOW_BASES", &gui_options.draw_fortress_airbase, VMENU_BASES },
  { "SHOW_RESOURCES", &gui_options.draw_specials, VMENU_RESOURCES },
  { "SHOW_HUTS", &gui_options.draw_huts, VMENU_HUTS },
  { "SHOW_POLLUTION", &gui_options.draw_pollution, VMENU_POLLUTION },
  { "SHOW_CITIES", &gui_options.draw_cities, VMENU_CITIES },
  { "SHOW_UNITS", &gui_options.draw_units, VMENU_UNITS },
  { "SHOW_UNIT_SOLID_BG", &gui_options.solid_color_behind_units, VMENU_UNIT_SOLID_BG },
  { "SHOW_UNIT_SHIELDS", &gui_options.draw_unit_shields , VMENU_UNIT_SHIELDS },
  { "SHOW_STACK_SIZE", &gui_options.draw_unit_stack_size, VMENU_STACK_SIZE },
  { "SHOW_FOCUS_UNIT", &gui_options.draw_focus_unit, VMENU_FOCUS_UNIT },
  { "SHOW_FOG_OF_WAR", &gui_options.draw_fog_of_war, VMENU_FOW },

  { NULL, NULL, -1 }
};

/************************************************************************//**
  Common handling of view menu entry toggles.
****************************************************************************/
static void view_menu_item_toggle(void (*cb)(void),
                                  bool updt_sensitivity,
                                  gpointer data)
{
  int i;
  struct menu_entry_info *info = (struct menu_entry_info *)data;

  info->state ^= 1;

  cb();

  if (updt_sensitivity) {
    view_menu_update_sensitivity(G_ACTION_MAP(gui_app()));
  }

  /* TODO: Make the information available directly from menu_entry_info,
   *       se we don't need to do this search for it */
  for (i = 0; meoms[i].menu_entry != NULL; i++) {
    if (!strcmp(meoms[i].menu_entry, info->key)) {
      g_menu_remove(view_menu, meoms[i].menu_pos);
      menu_item_insert_unref(view_menu, meoms[i].menu_pos,
                             create_toggle_menu_item_for_key(info->key));
      return;
    }
  }
}

/************************************************************************//**
  Item "SHOW_CITY_OUTLINES" callback.
****************************************************************************/
static void show_city_outlines_callback(GSimpleAction *action,
                                        GVariant *parameter,
                                        gpointer data)
{
  view_menu_item_toggle(key_city_outlines_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_CITY_OUTPUT" callback.
****************************************************************************/
static void show_city_output_callback(GSimpleAction *action,
                                      GVariant *parameter,
                                      gpointer data)
{
  view_menu_item_toggle(key_city_output_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_MAP_GRID" callback.
****************************************************************************/
static void show_map_grid_callback(GSimpleAction *action, GVariant *parameter,
                                   gpointer data)
{
  view_menu_item_toggle(key_map_grid_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_NAT_BORDERS" callback.
****************************************************************************/
static void show_national_borders_callback(GSimpleAction *action,
                                           GVariant *parameter,
                                           gpointer data)
{
  view_menu_item_toggle(key_map_borders_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_NATIVE_TILES" callback.
****************************************************************************/
static void show_native_tiles_callback(GSimpleAction *action,
                                       GVariant *parameter,
                                       gpointer data)
{
  view_menu_item_toggle(key_map_native_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_CITY_FULL_BAR" callback.
****************************************************************************/
static void show_city_full_bar_callback(GSimpleAction *action,
                                        GVariant *parameter,
                                        gpointer data)
{
  view_menu_item_toggle(key_city_full_bar_toggle, TRUE, data);
}

/************************************************************************//**
  Item "SHOW_CITY_NAMES" callback.
****************************************************************************/
static void show_city_names_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data)
{
  view_menu_item_toggle(key_city_names_toggle, TRUE, data);
}

/************************************************************************//**
  Item "SHOW_CITY_GROWTH" callback.
****************************************************************************/
static void show_city_growth_callback(GSimpleAction *action,
                                      GVariant *parameter,
                                      gpointer data)
{
  view_menu_item_toggle(key_city_growth_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_CITY_PRODUCTIONS" callback.
****************************************************************************/
static void show_city_productions_callback(GSimpleAction *action,
                                           GVariant *parameter,
                                           gpointer data)
{
  view_menu_item_toggle(key_city_productions_toggle, TRUE, data);
}

/************************************************************************//**
  Item "SHOW_CITY_BUY_COST" callback.
****************************************************************************/
static void show_city_buy_cost_callback(GSimpleAction *action,
                                        GVariant *parameter,
                                        gpointer data)
{
  view_menu_item_toggle(key_city_buycost_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_CITY_TRADE_ROUTES" callback.
****************************************************************************/
static void show_city_trade_routes_callback(GSimpleAction *action,
                                            GVariant *parameter,
                                            gpointer data)
{
  view_menu_item_toggle(key_city_trade_routes_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_TERRAIN" callback.
****************************************************************************/
static void show_terrain_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  view_menu_item_toggle(key_terrain_toggle, TRUE, data);
}

/************************************************************************//**
  Item "SHOW_COASTLINE" callback.
****************************************************************************/
static void show_coastline_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data)
{
  view_menu_item_toggle(key_coastline_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_PATHS" callback.
****************************************************************************/
static void show_paths_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data)
{
  view_menu_item_toggle(key_paths_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_IRRIGATION" callback.
****************************************************************************/
static void show_irrigation_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data)
{
  view_menu_item_toggle(key_irrigation_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_MINES" callback.
****************************************************************************/
static void show_mines_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data)
{
  view_menu_item_toggle(key_mines_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_BASES" callback.
****************************************************************************/
static void show_bases_callback(GSimpleAction *action, GVariant *parameter,
                                gpointer data)
{
  view_menu_item_toggle(key_bases_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_RESOURCES" callback.
****************************************************************************/
static void show_resources_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data)
{
  view_menu_item_toggle(key_resources_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_HUTS" callback.
****************************************************************************/
static void show_huts_callback(GSimpleAction *action, GVariant *parameter,
                               gpointer data)
{
  view_menu_item_toggle(key_huts_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_POLLUTION" callback.
****************************************************************************/
static void show_pollution_callback(GSimpleAction *action, GVariant *parameter,
                                    gpointer data)
{
  view_menu_item_toggle(key_pollution_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_CITIES" callback.
****************************************************************************/
static void show_cities_callback(GSimpleAction *action, GVariant *parameter,
                                 gpointer data)
{
  view_menu_item_toggle(key_cities_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_UNITS" callback.
****************************************************************************/
static void show_units_callback(GSimpleAction *action, GVariant *parameter,
                                gpointer data)
{
  view_menu_item_toggle(key_units_toggle, TRUE, data);
}

/************************************************************************//**
  Item "SHOW_UNIT_SOLID_BG" callback.
****************************************************************************/
static void show_unit_solid_bg_callback(GSimpleAction *action,
                                        GVariant *parameter,
                                        gpointer data)
{
  view_menu_item_toggle(key_unit_solid_bg_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_UNIT_SHIELDS" callback.
****************************************************************************/
static void show_unit_shields_callback(GSimpleAction *action,
                                       GVariant *parameter,
                                       gpointer data)
{
  view_menu_item_toggle(key_unit_shields_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_STACK_SIZE" callback.
****************************************************************************/
static void show_stack_size_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data)
{
  view_menu_item_toggle(key_unit_stack_size_toggle, FALSE, data);
}

/************************************************************************//**
  Item "SHOW_FOCUS_UNIT" callback.
****************************************************************************/
static void show_focus_unit_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data)
{
  view_menu_item_toggle(key_focus_unit_toggle, TRUE, data);
}

/************************************************************************//**
  Item "SHOW_FOG_OF_WAR" callback.
****************************************************************************/
static void show_fog_of_war_callback(GSimpleAction *action, GVariant *parameter,
                                     gpointer data)
{
  view_menu_item_toggle(key_fog_of_war_toggle, TRUE, data);
}

/************************************************************************//**
  Item "FULL_SCREEN" callback.
****************************************************************************/
static void full_screen_callback(GSimpleAction *action, GVariant *parameter,
                                 gpointer data)
{
  struct menu_entry_info *info = (struct menu_entry_info *)data;

  info->state ^= 1;
  GUI_GTK_OPTION(fullscreen) = info->state;

  fullscreen_opt_refresh(NULL);

  g_menu_remove(view_menu, VMENU_FULL_SCREEN);

  menu_item_insert_unref(view_menu, VMENU_FULL_SCREEN,
                         create_toggle_menu_item(info));
}

/************************************************************************//**
  Item "TOGGLE_FOG" callback.
****************************************************************************/
static void toggle_fog_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data)
{
  struct menu_entry_info *info = (struct menu_entry_info *)data;

  info->state ^= 1;

  key_editor_toggle_fogofwar();

  g_menu_remove(edit_menu, 5);

  menu_item_insert_unref(edit_menu, 5,
                         create_toggle_menu_item(info));
}

/************************************************************************//**
  Item "SCENARIO_PROPERTIES" callback.
****************************************************************************/
static void scenario_properties_callback(GSimpleAction *action,
                                         GVariant *parameter,
                                         gpointer data)
{
  struct property_editor *pe;

  pe = editprop_get_property_editor();
  property_editor_reload(pe, OBJTYPE_GAME);
  property_editor_popup(pe, OBJTYPE_GAME);
}

/************************************************************************//**
  Item "SAVE_SCENARIO" callback.
****************************************************************************/
static void save_scenario_callback(GSimpleAction *action, GVariant *parameter,
                                   gpointer data)
{
  save_scenario_dialog_popup();
}

/************************************************************************//**
  Item "SELECT_SINGLE" callback.
****************************************************************************/
static void select_single_callback(GSimpleAction *action, GVariant *parameter,
                                   gpointer data)
{
  request_unit_select(get_units_in_focus(), SELTYPE_SINGLE, SELLOC_TILE);
}

/************************************************************************//**
  Item "SELECT_ALL_ON_TILE" callback.
****************************************************************************/
static void select_all_on_tile_callback(GSimpleAction *action,
                                        GVariant *parameter,
                                        gpointer data)
{
  request_unit_select(get_units_in_focus(), SELTYPE_ALL, SELLOC_TILE);
}

/************************************************************************//**
  Item "SELECT_SAME_TYPE_TILE" callback.
****************************************************************************/
static void select_same_type_tile_callback(GSimpleAction *action,
                                           GVariant *parameter,
                                           gpointer data)
{
  request_unit_select(get_units_in_focus(), SELTYPE_SAME, SELLOC_TILE);
}

/************************************************************************//**
  Item "SELECT_SAME_TYPE_CONT" callback.
****************************************************************************/
static void select_same_type_cont_callback(GSimpleAction *action,
                                           GVariant *parameter,
                                           gpointer data)
{
  request_unit_select(get_units_in_focus(), SELTYPE_SAME, SELLOC_CONT);
}

/************************************************************************//**
  Item "SELECT_SAME_TYPE" callback.
****************************************************************************/
static void select_same_type_callback(GSimpleAction *action,
                                      GVariant *parameter,
                                      gpointer data)
{
  request_unit_select(get_units_in_focus(), SELTYPE_SAME, SELLOC_WORLD);
}

/************************************************************************//**
  Open unit selection dialog.
****************************************************************************/
static void select_dialog_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data)
{
  unit_select_dialog_popup(NULL);
}

/************************************************************************//**
  Open rally point dialog.
****************************************************************************/
static void rally_dialog_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  rally_dialog_popup();
}

/************************************************************************//**
  Open infra placement dialog.
****************************************************************************/
static void infra_dialog_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  infra_dialog_popup();
}

/************************************************************************//**
  Item "UNIT_WAIT" callback.
****************************************************************************/
static void unit_wait_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data)
{
  key_unit_wait();
}

/************************************************************************//**
  Item "UNIT_DONE" callback.
****************************************************************************/
static void unit_done_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data)
{
  key_unit_done();
}

/************************************************************************//**
  Item "UNIT_GOTO" callback.
****************************************************************************/
static void unit_goto_callback(GSimpleAction *action, GVariant *parameter,
                               gpointer data)
{
  key_unit_goto();
}

/************************************************************************//**
  Activate the goto system with an action to perform once there.
****************************************************************************/
static void unit_goto_and_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data)
{
  int sub_target = NO_TARGET;
  struct action *paction = (struct action *)data;

  fc_assert_ret(paction != NULL);

  switch (action_get_sub_target_kind(paction)) {
  case ASTK_BUILDING:
    {
      struct impr_type *pbuilding = g_object_get_data(G_OBJECT(action),
                                                      "end_building");
      fc_assert_ret(pbuilding != NULL);
      sub_target = improvement_number(pbuilding);
    }
    break;
  case ASTK_TECH:
    {
      struct advance *ptech = g_object_get_data(G_OBJECT(action),
                                                "end_tech");
      fc_assert_ret(ptech != NULL);
      sub_target = advance_number(ptech);
    }
    break;
  case ASTK_EXTRA:
  case ASTK_EXTRA_NOT_THERE:
    {
      struct extra_type *pextra = g_object_get_data(G_OBJECT(action),
                                                    "end_extra");
      fc_assert_ret(pextra != NULL);
      sub_target = extra_number(pextra);
    }
    break;
  case ASTK_SPECIALIST:
    {
      struct specialist *pspec = g_object_get_data(G_OBJECT(action),
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
static void unit_goto_city_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data)
{
  if (get_num_units_in_focus() > 0) {
    popup_goto_dialog();
  }
}

/************************************************************************//**
  Item "UNIT_RETURN" callback.
****************************************************************************/
static void unit_return_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data)
{
  unit_list_iterate(get_units_in_focus(), punit) {
    request_unit_return(punit);
  } unit_list_iterate_end;
}

/************************************************************************//**
  Item "UNIT_EXPLORE" callback.
****************************************************************************/
static void unit_explore_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  key_unit_auto_explore();
}

/************************************************************************//**
  Item "UNIT_PATROL" callback.
****************************************************************************/
static void unit_patrol_callback(GSimpleAction *action, GVariant *parameter,
                                 gpointer data)
{
  key_unit_patrol();
}

/************************************************************************//**
  Item "UNIT_TELEPORT" callback.
****************************************************************************/
static void unit_teleport_callback(GSimpleAction *action, GVariant *parameter,
                                   gpointer data)
{
  key_unit_teleport();
}

/************************************************************************//**
  Item "UNIT_SENTRY" callback.
****************************************************************************/
static void unit_sentry_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data)
{
  key_unit_sentry();
}

/************************************************************************//**
  Item "UNSENTRY_ALL" callback.
****************************************************************************/
static void unsentry_all_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  key_unit_wakeup_others();
}

/************************************************************************//**
  Action "FORTIFY" callback.
****************************************************************************/
static void fortify_callback(GSimpleAction *action,
                             GVariant *parameter,
                             gpointer data)
{
  unit_list_iterate(get_units_in_focus(), punit) {
    request_unit_fortify(punit);
  } unit_list_iterate_end;
}

/************************************************************************//**
  Item "UNIT_BOARD" callback.
****************************************************************************/
static void unit_board_callback(GSimpleAction *action, GVariant *parameter,
                                gpointer data)
{
  unit_list_iterate(get_units_in_focus(), punit) {
    request_transport(punit, unit_tile(punit));
  } unit_list_iterate_end;
}

/************************************************************************//**
  Item "UNIT_DEBOARD" callback.
****************************************************************************/
static void unit_deboard_callback(GSimpleAction *action, GVariant *parameter,
                                  gpointer data)
{
  unit_list_iterate(get_units_in_focus(), punit) {
    request_unit_unload(punit);
  } unit_list_iterate_end;
}

/************************************************************************//**
  Item "UNIT_UNLOAD_TRANSPORTER" callback.
****************************************************************************/
static void unit_unload_transporter_callback(GSimpleAction *action,
                                             GVariant *parameter,
                                             gpointer data)
{
  key_unit_unload_all();
}

/************************************************************************//**
  Item "UNIT_HOMECITY" callback.
****************************************************************************/
static void unit_homecity_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data)
{
  key_unit_homecity();
}

/************************************************************************//**
  Item "UNIT_UPGRADE" callback.
****************************************************************************/
static void unit_upgrade_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  popup_upgrade_dialog(get_units_in_focus());
}

/************************************************************************//**
  Item "UNIT_CONVERT" callback.
****************************************************************************/
static void unit_convert_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  key_unit_convert();
}

/************************************************************************//**
  Item "UNIT_DISBAND" callback.
****************************************************************************/
static void unit_disband_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  popup_disband_dialog(get_units_in_focus());
}

/************************************************************************//**
  Item "BUILD_CITY" callback.
****************************************************************************/
static void build_city_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data)
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
static void auto_work_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data)
{
  key_unit_auto_work();
}

/************************************************************************//**
  Action "BUILD_ROAD" callback.
****************************************************************************/
static void build_road_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data)
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
static void build_irrigation_callback(GSimpleAction *action,
                                      GVariant *parameter,
                                      gpointer data)
{
  key_unit_irrigate();
}

/************************************************************************//**
  Action "CULTIVATE" callback.
****************************************************************************/
static void cultivate_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data)
{
  key_unit_cultivate();
}

/************************************************************************//**
  Action "PLANT" callback.
****************************************************************************/
static void plant_callback(GSimpleAction *action,
                           GVariant *parameter,
                           gpointer data)
{
  key_unit_plant();
}

/************************************************************************//**
  Action "BUILD_MINE" callback.
****************************************************************************/
static void build_mine_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data)
{
  key_unit_mine();
}

/************************************************************************//**
  Action "CONNECT_ROAD" callback.
****************************************************************************/
static void connect_road_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
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
static void connect_rail_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
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
static void connect_maglev_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data)
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
static void connect_irrigation_callback(GSimpleAction *action,
                                        GVariant *parameter,
                                        gpointer data)
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
static void transform_terrain_callback(GSimpleAction *action,
                                       GVariant *parameter,
                                       gpointer data)
{
  key_unit_transform();
}

/************************************************************************//**
  Action "CLEAN" callback.
****************************************************************************/
static void clean_callback(GSimpleAction *action, GVariant *parameter,
                           gpointer data)
{
  key_unit_clean();
}

/************************************************************************//**
  Action "BUILD_FORTRESS" callback.
****************************************************************************/
static void build_fortress_callback(GSimpleAction *action, GVariant *parameter,
                                    gpointer data)
{
  key_unit_fortress();
}

/************************************************************************//**
  Action "BUILD_AIRBASE" callback.
****************************************************************************/
static void build_airbase_callback(GSimpleAction *action, GVariant *parameter,
                                   gpointer data)
{
  key_unit_airbase();
}

/************************************************************************//**
  Action "PARADROP" callback.
****************************************************************************/
static void paradrop_callback(GSimpleAction *action,
                              GVariant *parameter,
                              gpointer data)
{
  key_unit_paradrop();
}

/************************************************************************//**
  Action "PILLAGE" callback.
****************************************************************************/
static void pillage_callback(GSimpleAction *action,
                             GVariant *parameter,
                             gpointer data)
{
  key_unit_pillage();
}

/************************************************************************//**
  Action "DO_ACTION" callback.
****************************************************************************/
static void do_action_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data)
{
  key_unit_action_select_tgt();
}

/************************************************************************//**
  Action "TAX_RATES" callback.
****************************************************************************/
static void tax_rate_callback(GSimpleAction *action, GVariant *parameter,
                              gpointer data)
{
  popup_rates_dialog();
}

/************************************************************************//**
  Action "MULTIPLIERS" callback.
****************************************************************************/
static void multiplier_callback(GSimpleAction *action, GVariant *parameter,
                                gpointer data)
{
  popup_multiplier_dialog();
}

/************************************************************************//**
  The player has chosen a government from the menu.
****************************************************************************/
static void government_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data)
{
  popup_revolution_dialog((struct government *) data);
}

/************************************************************************//**
  The player has chosen targetless revolution from the menu.
****************************************************************************/
static void revolution_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data)
{
  popup_revolution_dialog(NULL);
}

/************************************************************************//**
  The player has chosen a base to build from the menu.
****************************************************************************/
static void base_callback(GSimpleAction *action,
                          GVariant *parameter,
                          gpointer data)
{
  struct extra_type *pextra = data;

  unit_list_iterate(get_units_in_focus(), punit) {
    request_new_unit_activity_targeted(punit, ACTIVITY_BASE, pextra);
  } unit_list_iterate_end;
}

/************************************************************************//**
  The player has chosen a road to build from the menu.
****************************************************************************/
static void road_callback(GSimpleAction *action,
                          GVariant *parameter,
                          gpointer data)
{
  struct extra_type *pextra = data;

  unit_list_iterate(get_units_in_focus(), punit) {
    request_new_unit_activity_targeted(punit, ACTIVITY_GEN_ROAD,
                                       pextra);
  } unit_list_iterate_end;
}

/************************************************************************//**
  The player has chosen an irrigation to build from the menu.
****************************************************************************/
static void irrigation_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data)
{
  struct extra_type *pextra = data;

  unit_list_iterate(get_units_in_focus(), punit) {
    request_new_unit_activity_targeted(punit, ACTIVITY_IRRIGATE,
                                       pextra);
  } unit_list_iterate_end;
}

/************************************************************************//**
  The player has chosen a mine to build from the menu.
****************************************************************************/
static void mine_callback(GSimpleAction *action,
                          GVariant *parameter,
                          gpointer data)
{
  struct extra_type *pextra = data;

  unit_list_iterate(get_units_in_focus(), punit) {
    request_new_unit_activity_targeted(punit, ACTIVITY_MINE,
                                       pextra);
  } unit_list_iterate_end;
}

/************************************************************************//**
  The player has chosen an extra to clean from the menu.
****************************************************************************/
static void clean_menu_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data)
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
static void center_view_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data)
{
  center_on_unit();
}

/************************************************************************//**
  Action "REPORT_UNITS" callback.
****************************************************************************/
static void report_units_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  units_report_dialog_popup(TRUE);
}

/************************************************************************//**
  Action "REPORT_CITIES" callback.
****************************************************************************/
static void report_cities_callback(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer data)

{
  city_report_dialog_popup(TRUE);
}

/************************************************************************//**
  Action "REPORT_ECONOMY" callback.
****************************************************************************/
static void report_economy_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data)
{
  economy_report_dialog_popup(TRUE);
}

/************************************************************************//**
  Action "REPORT_RESEARCH" callback.
****************************************************************************/
static void report_research_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data)
{
  science_report_dialog_popup(TRUE);
}

/************************************************************************//**
  Action "REPORT_SPACESHIP" callback.
****************************************************************************/
static void report_spaceship_callback(GSimpleAction *action,
                                      GVariant *parameter,
                                      gpointer data)
{
  if (NULL != client.conn.playing) {
    popup_spaceship_dialog(client.conn.playing);
  }
}

/************************************************************************//**
  Select battle group
****************************************************************************/
static void bg_select_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data)
{
  key_unit_select_battlegroup(GPOINTER_TO_INT(data), FALSE);
}

/************************************************************************//**
  Assign units to battle group
****************************************************************************/
static void bg_assign_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data)
{
  key_unit_assign_battlegroup(GPOINTER_TO_INT(data), FALSE);
}

/************************************************************************//**
  Append units to battle group
****************************************************************************/
static void bg_append_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data)
{
  key_unit_assign_battlegroup(GPOINTER_TO_INT(data), TRUE);
}

/************************************************************************//**
  Create toggle menu entry by info.
  Caller need to g_object_unref() returned item.
****************************************************************************/
static GMenuItem *create_toggle_menu_item(struct menu_entry_info *info)
{
  char actname[150];
  GMenuItem *item;

  fc_snprintf(actname, sizeof(actname), "app.%s(%s)",
              info->action, info->state ? "true" : "false");
  item = g_menu_item_new(Q_(info->name), NULL);
  g_menu_item_set_detailed_action(item, actname);

  if (info->accl != NULL) {
    const char *accls[2] = { info->accl, NULL };

    gtk_application_set_accels_for_action(gui_app(), actname, accls);
  }

  return item;
}

/************************************************************************//**
  Create toggle menu entry by key
  Caller need to g_object_unref() returned item.
****************************************************************************/
static GMenuItem *create_toggle_menu_item_for_key(const char *key)
{
  return create_toggle_menu_item(menu_entry_info_find(key));
}

/************************************************************************//**
  Set name of the menu item.
****************************************************************************/
static void menu_entry_init(GMenu *sub, const char *key)
{
  struct menu_entry_info *info = menu_entry_info_find(key);

  if (info != NULL) {
    GMenuItem *item;

    if (info->toggle != NULL) {
      item = create_toggle_menu_item(info);
    } else {
      char actname[150];

      fc_snprintf(actname, sizeof(actname), "app.%s", info->action);
      item = g_menu_item_new(Q_(info->name), actname);
    }

    /* Should be menu_item_append_unref()? */
    g_menu_append_item(sub, item);
    g_object_unref(item);
  }
}

/************************************************************************//**
  Registers menu actions for the application.
****************************************************************************/
void menus_set_initial_toggle_values(void)
{
  int i;

  for (i = 0; meoms[i].menu_entry != NULL; i++) {
    struct menu_entry_info *info = menu_entry_info_find(meoms[i].menu_entry);

    if (meoms[i].option != NULL) {
      info->state = *meoms[i].option;
    } else {
      info->state = FALSE; /* Best guess we have */
    }
  }
}

/************************************************************************//**
  Registers menu actions for the application.
****************************************************************************/
static void setup_app_actions(GApplication *fc_app)
{
  int i;
  GVariantType *bvart = g_variant_type_new("b");

  /* Simple entries */
  g_action_map_add_action_entries(G_ACTION_MAP(fc_app), acts,
                                  G_N_ELEMENTS(acts), fc_app);

  /* Toggles */
  for (i = 0; menu_entries[i].key != NULL; i++) {
    if (menu_entries[i].toggle != NULL) {
      GSimpleAction *act;
      GVariant *var = g_variant_new("b", TRUE);

      act = g_simple_action_new_stateful(menu_entries[i].action, bvart, var);
      g_action_map_add_action(G_ACTION_MAP(fc_app), G_ACTION(act));
      g_signal_connect(act, "change-state",
                       G_CALLBACK(menu_entries[i].toggle),
                       (gpointer)&(menu_entries[i]));
    }
  }

  g_variant_type_free(bvart);

  for (i = 0; menu_entries[i].key != NULL; i++) {
    if (menu_entries[i].accl != NULL && menu_entries[i].toggle == NULL) {
      const char *accls[2] = { menu_entries[i].accl, NULL };
      char actname[150];

      fc_snprintf(actname, sizeof(actname), "app.%s", menu_entries[i].action);

      gtk_application_set_accels_for_action(GTK_APPLICATION(fc_app), actname, accls);
    }
  }
}

/************************************************************************//**
  Registers menu actions for Battle Groups actions
****************************************************************************/
static void register_bg_actions(GActionMap *map, int bg)
{
  GSimpleAction *act;
  char actname[256];

  fc_snprintf(actname, sizeof(actname), "bg_select_%d", bg);
  act = g_simple_action_new(actname, NULL);
  g_action_map_add_action(map, G_ACTION(act));
  g_signal_connect(act, "activate",
                   G_CALLBACK(bg_select_callback), GINT_TO_POINTER(bg));

  fc_snprintf(actname, sizeof(actname), "bg_assign_%d", bg);
  act = g_simple_action_new(actname, NULL);
  g_action_map_add_action(map, G_ACTION(act));
  g_signal_connect(act, "activate",
                   G_CALLBACK(bg_assign_callback), GINT_TO_POINTER(bg));

  fc_snprintf(actname, sizeof(actname), "bg_append_%d", bg);
  act = g_simple_action_new(actname, NULL);
  g_action_map_add_action(map, G_ACTION(act));
  g_signal_connect(act, "activate",
                   G_CALLBACK(bg_append_callback), GINT_TO_POINTER(bg));
}

/************************************************************************//**
  Creates the menu bar.
****************************************************************************/
static GMenu *setup_menus(GtkApplication *app)
{
  GMenu *menubar;
  GMenu *topmenu;
  GMenu *submenu;
  int i;

  fc_assert(!menus_built);

  menubar = g_menu_new();

  topmenu = g_menu_new();
  menu_entry_init(topmenu, "CLEAR_CHAT_LOGS");
  menu_entry_init(topmenu, "SAVE_CHAT_LOGS");

  options_menu = g_menu_new();
  menu_entry_init(options_menu, "LOCAL_OPTIONS");
  menu_entry_init(options_menu, "MESSAGE_OPTIONS");
  menu_entry_init(options_menu, "SERVER_OPTIONS");
  menu_entry_init(options_menu, "SAVE_OPTIONS");
  menu_entry_init(options_menu, "SAVE_OPTIONS_ON_EXIT");

  submenu_append_unref(topmenu, _("_Options"), G_MENU_MODEL(options_menu));

#ifdef FREECIV_DEBUG
  menu_entry_init(topmenu, "RELOAD_TILESET");
#endif /* FREECIV_DEBUG */

  menu_entry_init(topmenu, "GAME_SAVE");
  menu_entry_init(topmenu, "GAME_SAVE_AS");
  menu_entry_init(topmenu, "MAPIMG_SAVE");
  menu_entry_init(topmenu, "MAPIMG_SAVE_AS");
  menu_entry_init(topmenu, "VOLUME_UP");
  menu_entry_init(topmenu, "VOLUME_DOWN");
  menu_entry_init(topmenu, "LEAVE");
  menu_entry_init(topmenu, "QUIT");

  submenu_append_unref(menubar, _("_Game"), G_MENU_MODEL(topmenu));

  edit_menu = g_menu_new();

  menu_entry_init(edit_menu, "FIND_CITY");
  menu_entry_init(edit_menu, "WORKLISTS");
  menu_entry_init(edit_menu, "RALLY_DLG");
  menu_entry_init(edit_menu, "INFRA_DLG");
  menu_entry_init(edit_menu, "EDIT_MODE");
  menu_entry_init(edit_menu, "TOGGLE_FOG");
  menu_entry_init(edit_menu, "SCENARIO_PROPERTIES");
  menu_entry_init(edit_menu, "SCENARIO_SAVE");
  menu_entry_init(edit_menu, "CLIENT_LUA_SCRIPT");

  submenu_append_unref(menubar, _("_Edit"), G_MENU_MODEL(edit_menu));

  view_menu = g_menu_new();

  menu_entry_init(view_menu, "SHOW_CITY_OUTLINES");
  menu_entry_init(view_menu, "SHOW_CITY_OUTPUT");
  menu_entry_init(view_menu, "SHOW_MAP_GRID");
  menu_entry_init(view_menu, "SHOW_NAT_BORDERS");
  menu_entry_init(view_menu, "SHOW_NATIVE_TILES");
  menu_entry_init(view_menu, "SHOW_CITY_FULL_BAR");
  menu_entry_init(view_menu, "SHOW_CITY_NAMES");
  menu_entry_init(view_menu, "SHOW_CITY_GROWTH");
  menu_entry_init(view_menu, "SHOW_CITY_PRODUCTIONS");
  menu_entry_init(view_menu, "SHOW_CITY_BUY_COST");
  menu_entry_init(view_menu, "SHOW_CITY_TRADE_ROUTES");
  menu_entry_init(view_menu, "SHOW_TERRAIN");
  menu_entry_init(view_menu, "SHOW_COASTLINE");
  menu_entry_init(view_menu, "SHOW_PATHS");
  menu_entry_init(view_menu, "SHOW_IRRIGATION");
  menu_entry_init(view_menu, "SHOW_MINES");
  menu_entry_init(view_menu, "SHOW_BASES");
  menu_entry_init(view_menu, "SHOW_RESOURCES");
  menu_entry_init(view_menu, "SHOW_HUTS");
  menu_entry_init(view_menu, "SHOW_POLLUTION");
  menu_entry_init(view_menu, "SHOW_CITIES");
  menu_entry_init(view_menu, "SHOW_UNITS");
  menu_entry_init(view_menu, "SHOW_UNIT_SOLID_BG");
  menu_entry_init(view_menu, "SHOW_UNIT_SHIELDS");
  menu_entry_init(view_menu, "SHOW_STACK_SIZE");
  menu_entry_init(view_menu, "SHOW_FOCUS_UNIT");

  menu_entry_init(view_menu, "SHOW_FOG_OF_WAR");

  menu_entry_init(view_menu, "FULL_SCREEN");
  menu_entry_init(view_menu, "CENTER_VIEW");

  submenu_append_unref(menubar, Q_("?verb:_View"), G_MENU_MODEL(view_menu));

  topmenu = g_menu_new();

  menu_entry_init(topmenu, "SELECT_SINGLE");
  menu_entry_init(topmenu, "SELECT_ALL_ON_TILE");
  menu_entry_init(topmenu, "SELECT_SAME_TYPE_TILE");
  menu_entry_init(topmenu, "SELECT_SAME_TYPE_CONT");
  menu_entry_init(topmenu, "SELECT_SAME_TYPE");
  menu_entry_init(topmenu, "SELECT_DLG");

  submenu_append_unref(menubar, _("_Select"), G_MENU_MODEL(topmenu));

  unit_menu = g_menu_new();

  menu_entry_init(unit_menu, "UNIT_GOTO");

  /* Placeholder submenu (so that menu update has something to replace) */
  submenu = g_menu_new();
  submenu_append_unref(unit_menu, N_("Go to a_nd..."), G_MENU_MODEL(submenu));

  menu_entry_init(unit_menu, "UNIT_GOTO_CITY");
  menu_entry_init(unit_menu, "UNIT_RETURN");
  menu_entry_init(unit_menu, "UNIT_EXPLORE");
  menu_entry_init(unit_menu, "UNIT_PATROL");
  menu_entry_init(unit_menu, "UNIT_TELEPORT");
  menu_entry_init(unit_menu, "UNIT_SENTRY");
  menu_entry_init(unit_menu, "UNSENTRY_ALL");
  menu_entry_init(unit_menu, "UNIT_BOARD");
  menu_entry_init(unit_menu, "UNIT_DEBOARD");
  menu_entry_init(unit_menu, "UNIT_UNLOAD_TRANSPORTER");
  menu_entry_init(unit_menu, "UNIT_HOMECITY");
  menu_entry_init(unit_menu, "UNIT_UPGRADE");
  menu_entry_init(unit_menu, "UNIT_CONVERT");
  menu_entry_init(unit_menu, "UNIT_DISBAND");
  menu_entry_init(unit_menu, "DO_ACTION");
  menu_entry_init(unit_menu, "UNIT_WAIT");
  menu_entry_init(unit_menu, "UNIT_DONE");

  submenu_append_unref(menubar, _("_Unit"), G_MENU_MODEL(unit_menu));

  work_menu = g_menu_new();
  menu_entry_init(work_menu, "BUILD_CITY");
  menu_entry_init(work_menu, "AUTO_WORKER");

  /* Placeholder submenus (so that menu update has something to replace) */
  submenu = g_menu_new();
  submenu_append_unref(work_menu, _("Build _Path"), G_MENU_MODEL(submenu));
  submenu = g_menu_new();
  submenu_append_unref(work_menu, _("Build _Irrigation"), G_MENU_MODEL(submenu));
  submenu = g_menu_new();
  submenu_append_unref(work_menu, _("Build _Mine"), G_MENU_MODEL(submenu));
  submenu = g_menu_new();
  submenu_append_unref(work_menu, _("_Clean Nuisance"), G_MENU_MODEL(submenu));

  menu_entry_init(work_menu, "BUILD_ROAD");
  menu_entry_init(work_menu, "BUILD_IRRIGATION");
  menu_entry_init(work_menu, "BUILD_MINE");
  menu_entry_init(work_menu, "CULTIVATE");
  menu_entry_init(work_menu, "PLANT");
  menu_entry_init(work_menu, "TRANSFORM_TERRAIN");
  menu_entry_init(work_menu, "CONNECT_ROAD");
  menu_entry_init(work_menu, "CONNECT_RAIL");
  menu_entry_init(work_menu, "CONNECT_MAGLEV");
  menu_entry_init(work_menu, "CONNECT_IRRIGATION");
  menu_entry_init(work_menu, "CLEAN");

  submenu_append_unref(menubar, _("_Work"), G_MENU_MODEL(work_menu));

  combat_menu = g_menu_new();
  menu_entry_init(combat_menu, "FORTIFY");
  menu_entry_init(combat_menu, "BUILD_FORTRESS");
  menu_entry_init(combat_menu, "BUILD_AIRBASE");

  /* Placeholder submenu (so that menu update has something to replace) */
  submenu = g_menu_new();
  submenu_append_unref(combat_menu, _("Build _Base"), G_MENU_MODEL(submenu));

  submenu_append_unref(menubar, _("_Combat"), G_MENU_MODEL(combat_menu));

  menu_entry_init(combat_menu, "PARADROP");
  menu_entry_init(combat_menu, "PILLAGE");

  gov_menu = g_menu_new();

  menu_entry_init(gov_menu, "MAP_VIEW");
  menu_entry_init(gov_menu, "REPORT_UNITS");
  menu_entry_init(gov_menu, "REPORT_NATIONS");
  menu_entry_init(gov_menu, "REPORT_CITIES");
  menu_entry_init(gov_menu, "REPORT_ECONOMY");
  menu_entry_init(gov_menu, "REPORT_RESEARCH");
  menu_entry_init(gov_menu, "TAX_RATES");
  menu_entry_init(gov_menu, "POLICIES");

  /* Placeholder submenu (so that menu update has something to replace) */
  submenu = g_menu_new();
  submenu_append_unref(gov_menu, _("_Government"), G_MENU_MODEL(submenu));

  menu_entry_init(gov_menu, "REPORT_WOW");
  menu_entry_init(gov_menu, "REPORT_TOP_CITIES");
  menu_entry_init(gov_menu, "REPORT_MESSAGES");
  menu_entry_init(gov_menu, "REPORT_DEMOGRAPHIC");
  menu_entry_init(gov_menu, "REPORT_SPACESHIP");
  menu_entry_init(gov_menu, "REPORT_ACHIEVEMENTS");

  submenu_append_unref(menubar, _("C_ivilization"), G_MENU_MODEL(gov_menu));

  topmenu = g_menu_new();

  /* Keys exist for 4 battle groups */
  FC_STATIC_ASSERT(MAX_NUM_BATTLEGROUPS == 4, incompatible_bg_count);

  for (i = 0; i < MAX_NUM_BATTLEGROUPS; i++) {
    char key[128];

    /* User side battle group numbers start from 1 */
    fc_snprintf(key, sizeof(key), "BG_SELECT_%d", i + 1);
    menu_entry_init(topmenu, key);
    fc_snprintf(key, sizeof(key), "BG_ASSIGN_%d", i + 1);
    menu_entry_init(topmenu, key);
    fc_snprintf(key, sizeof(key), "BG_APPEND_%d", i + 1);
    menu_entry_init(topmenu, key);

    register_bg_actions(G_ACTION_MAP(app), i);
  }

  submenu_append_unref(menubar, _("_Battle Groups"), G_MENU_MODEL(topmenu));

  topmenu = g_menu_new();

  menu_entry_init(topmenu, "HELP_OVERVIEW");
  menu_entry_init(topmenu, "HELP_PLAYING");
  menu_entry_init(topmenu, "HELP_POLICIES");
  menu_entry_init(topmenu, "HELP_TERRAIN");
  menu_entry_init(topmenu, "HELP_ECONOMY");
  menu_entry_init(topmenu, "HELP_CITIES");
  menu_entry_init(topmenu, "HELP_IMPROVEMENTS");
  menu_entry_init(topmenu, "HELP_WONDERS");
  menu_entry_init(topmenu, "HELP_UNITS");
  menu_entry_init(topmenu, "HELP_COMBAT");
  menu_entry_init(topmenu, "HELP_ZOC");
  menu_entry_init(topmenu, "HELP_GOVERNMENT");
  menu_entry_init(topmenu, "HELP_DIPLOMACY");
  menu_entry_init(topmenu, "HELP_TECH");
  menu_entry_init(topmenu, "HELP_SPACE_RACE");
  menu_entry_init(topmenu, "HELP_RULESET");
  menu_entry_init(topmenu, "HELP_TILESET");
  menu_entry_init(topmenu, "HELP_MUSICSET");
  menu_entry_init(topmenu, "HELP_NATIONS");
  menu_entry_init(topmenu, "HELP_CONNECTING");
  menu_entry_init(topmenu, "HELP_CONTROLS");
  menu_entry_init(topmenu, "HELP_GOVERNOR");
  menu_entry_init(topmenu, "HELP_CHATLINE");
  menu_entry_init(topmenu, "HELP_WORKLIST_EDITOR");
  menu_entry_init(topmenu, "HELP_LANGUAGES");
  menu_entry_init(topmenu, "HELP_COPYING");
  menu_entry_init(topmenu, "HELP_ABOUT");

  submenu_append_unref(menubar, _("_Help"), G_MENU_MODEL(topmenu));

  menus_built = TRUE;

  real_menus_update();

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
  Sets sensitivity of an menu entry, found by info.
****************************************************************************/
static void menu_entry_set_sensitive_info(GActionMap *map,
                                          struct menu_entry_info *info,
                                          gboolean is_enabled)
{
  GAction *act = g_action_map_lookup_action(map, info->action);

  if (act != NULL) {
    g_simple_action_set_enabled(G_SIMPLE_ACTION(act), is_enabled);
  }
}

/************************************************************************//**
  Sets sensitivity of an menu entry.
****************************************************************************/
static void menu_entry_set_sensitive(GActionMap *map,
                                     const char *key,
                                     gboolean is_enabled)
{
  struct menu_entry_info *info = menu_entry_info_find(key);

  if (info != NULL) {
    menu_entry_set_sensitive_info(map, info, is_enabled);
  }
}

/************************************************************************//**
  Set sensitivity of all entries in the group.
****************************************************************************/
static void menu_entry_group_set_sensitive(GActionMap *map,
                                           enum menu_entry_grouping group,
                                           gboolean is_enabled)
{
  int i;

  for (i = 0; menu_entries[i].key != NULL; i++) {
    if (menu_entries[i].grouping & group) {
      menu_entry_set_sensitive_info(map, &(menu_entries[i]), is_enabled);
    }
  }
}

/************************************************************************//**
  Renames an action.
****************************************************************************/
static void menus_rename(GMenu *parent, int index, const char *key,
                         const char *new_name)
{
  struct menu_entry_info *info = menu_entry_info_find(key);

  if (info != NULL) {
    char actname[150];
    GMenuItem *item;

    fc_snprintf(actname, sizeof(actname), "app.%s", info->action);
    g_menu_remove(parent, index);

    item = g_menu_item_new(new_name, actname);
    menu_item_insert_unref(parent, index, item);
  }
}

/************************************************************************//**
  Update the sensitivity of the items in the view menu.
****************************************************************************/
static void view_menu_update_sensitivity(GActionMap *map)
{
  /* The "full" city bar (i.e. the new way of drawing the
   * city name), can draw the city growth even without drawing
   * the city name. But the old method cannot. */
  if (gui_options.draw_full_citybar) {
    menu_entry_set_sensitive(map, "SHOW_CITY_GROWTH", TRUE);
    menu_entry_set_sensitive(map, "SHOW_CITY_TRADE_ROUTES", TRUE);
  } else {
    menu_entry_set_sensitive(map, "SHOW_CITY_GROWTH", gui_options.draw_city_names);
    menu_entry_set_sensitive(map, "SHOW_CITY_TRADE_ROUTES",
                             gui_options.draw_city_names);
  }

  menu_entry_set_sensitive(map, "SHOW_CITY_BUY_COST",
                           gui_options.draw_city_productions);
  menu_entry_set_sensitive(map, "SHOW_COASTLINE", !gui_options.draw_terrain);
  menu_entry_set_sensitive(map, "SHOW_UNIT_SOLID_BG",
                           gui_options.draw_units || gui_options.draw_focus_unit);
  menu_entry_set_sensitive(map, "SHOW_UNIT_SHIELDS",
                           gui_options.draw_units || gui_options.draw_focus_unit);
  menu_entry_set_sensitive(map, "SHOW_FOCUS_UNIT", !gui_options.draw_units);
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
  GtkApplication *fc_app = gui_app();
  GActionMap *map = G_ACTION_MAP(fc_app);
  GMenu *submenu;
  int i, j;
  int tgt_kind_group;
  struct unit_list *punits;
  unsigned num_units;
  struct menu_entry_info *info;
  struct road_type *proad;
  struct extra_type_list *extras;
  bool conn_possible;

  if (!menus_built || client_state() == C_S_DISCONNECTED) {
    return;
  }

  punits = get_units_in_focus();

  submenu = g_menu_new();

  i = 0;
  j = 0;

  /* Add the new action entries grouped by target kind. */
  for (tgt_kind_group = 0; tgt_kind_group < ATK_COUNT; tgt_kind_group++) {
    action_noninternal_iterate(act_id) {
      struct action *paction = action_by_number(act_id);
      GSimpleAction *act;
      char actname[256];
      char name[256];

      if (action_id_get_actor_kind(act_id) != AAK_UNIT) {
        /* This action isn't performed by a unit. */
        continue;
      }

      if (action_id_get_target_kind(act_id) != tgt_kind_group) {
        /* Wrong group. */
        continue;
      }

      if (!units_can_do_action(punits, act_id, TRUE)) {
        continue;
      }

      /* Create and add the menu item. */
      fc_snprintf(actname, sizeof(actname), "action_%d", i);
      act = g_simple_action_new(actname, NULL);
      g_action_map_add_action(map, G_ACTION(act));

      fc_snprintf(name, sizeof(name), "%s",
                  action_get_ui_name_mnemonic(act_id, "_"));
      fc_snprintf(actname, sizeof(actname), "app.action_%d", i++);

      if (action_id_has_complex_target(act_id)) {
        GMenu *sub_target_menu = g_menu_new();
        char subname[256];

#define CREATE_SUB_ITEM(_sub_target_, _sub_target_key_, _sub_target_name_) \
{                                                                          \
  fc_snprintf(actname, sizeof(actname), "subtgt_%d", j);                   \
  act = g_simple_action_new(actname, NULL);                                \
  g_action_map_add_action(map, G_ACTION(act));                             \
  g_object_set_data(G_OBJECT(act), _sub_target_key_, _sub_target_);        \
  g_signal_connect(act, "activate", G_CALLBACK(unit_goto_and_callback),    \
                   paction);                                               \
  fc_snprintf(subname, sizeof(subname), "%s", _sub_target_name_);          \
  fc_snprintf(actname, sizeof(actname), "app.subtgt_%d", j++);             \
  menu_item_append_unref(sub_target_menu,                                  \
                         g_menu_item_new(subname, actname));               \
}

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

        submenu_append_unref(submenu, name, G_MENU_MODEL(sub_target_menu));
      } else {
        g_signal_connect(act, "activate",
                         G_CALLBACK(unit_goto_and_callback), paction);
        menu_item_append_unref(submenu, g_menu_item_new(name, actname));
      }
    } action_noninternal_iterate_end;
  }
  g_menu_remove(unit_menu, 1);
  g_menu_insert_submenu(unit_menu, 1, _("Go to a_nd..."), G_MENU_MODEL(submenu));

  submenu = g_menu_new();

  if (untargeted_revolution_allowed()) {
    menu_entry_init(submenu, "START_REVOLUTION");
  }

  i = 0;
  governments_iterate(g) {
    if (g != game.government_during_revolution) {
      char name[256];
      char actname[256];
      GSimpleAction *act;

      fc_snprintf(actname, sizeof(actname), "government_%d", i);
      act = g_simple_action_new(actname, NULL);
      g_simple_action_set_enabled(act, can_change_to_government(client_player(), g));
      g_action_map_add_action(map, G_ACTION(act));
      g_signal_connect(act, "activate", G_CALLBACK(government_callback), g);

      /* TRANS: %s is a government name */
      fc_snprintf(name, sizeof(name), _("%s..."),
                  government_name_translation(g));
      fc_snprintf(actname, sizeof(actname), "app.government_%d", i++);
      menu_item_append_unref(submenu, g_menu_item_new(name, actname));
    }
  } governments_iterate_end;
  g_menu_remove(gov_menu, 8);
  g_menu_insert_submenu(gov_menu, 8, _("_Government"), G_MENU_MODEL(submenu));

  submenu = g_menu_new();

  extra_type_by_cause_iterate(EC_ROAD, pextra) {
    if (pextra->buildable) {
      char actname[256];
      GSimpleAction *act;

      fc_snprintf(actname, sizeof(actname), "path_%d", i);
      act = g_simple_action_new(actname, NULL);
      g_simple_action_set_enabled(act,
                                  can_units_do_activity_targeted_client(punits,
                                                                        ACTIVITY_GEN_ROAD,
                                                                        pextra));
      g_action_map_add_action(map, G_ACTION(act));
      g_signal_connect(act, "activate", G_CALLBACK(road_callback), pextra);

      fc_snprintf(actname, sizeof(actname), "app.path_%d", i++);
      menu_item_append_unref(submenu,
                             g_menu_item_new(extra_name_translation(pextra), actname));
    }
  } extra_type_by_cause_iterate_end;

  g_menu_remove(work_menu, 2);
  g_menu_insert_submenu(work_menu, 2, _("Build _Path"), G_MENU_MODEL(submenu));

  submenu = g_menu_new();

  extra_type_by_cause_iterate(EC_IRRIGATION, pextra) {
    if (pextra->buildable) {
      char actname[256];
      GSimpleAction *act;

      fc_snprintf(actname, sizeof(actname), "irrig_%d", i);
      act = g_simple_action_new(actname, NULL);
      g_simple_action_set_enabled(act,
                                  can_units_do_activity_targeted_client(punits,
                                                                        ACTIVITY_IRRIGATE,
                                                                        pextra));
      g_action_map_add_action(map, G_ACTION(act));
      g_signal_connect(act, "activate", G_CALLBACK(irrigation_callback), pextra);

      fc_snprintf(actname, sizeof(actname), "app.irrig_%d", i++);
      menu_item_append_unref(submenu,
                             g_menu_item_new(extra_name_translation(pextra), actname));
    }
  } extra_type_by_cause_iterate_end;

  g_menu_remove(work_menu, 3);
  g_menu_insert_submenu(work_menu, 3, _("Build _Irrigation"), G_MENU_MODEL(submenu));

  submenu = g_menu_new();

  extra_type_by_cause_iterate(EC_MINE, pextra) {
    if (pextra->buildable) {
      char actname[256];
      GSimpleAction *act;

      fc_snprintf(actname, sizeof(actname), "mine_%d", i);
      act = g_simple_action_new(actname, NULL);
      g_simple_action_set_enabled(act,
                                  can_units_do_activity_targeted_client(punits,
                                                                        ACTIVITY_MINE,
                                                                        pextra));
      g_action_map_add_action(map, G_ACTION(act));
      g_signal_connect(act, "activate", G_CALLBACK(mine_callback), pextra);

      fc_snprintf(actname, sizeof(actname), "app.mine_%d", i++);
      menu_item_append_unref(submenu,
                             g_menu_item_new(extra_name_translation(pextra), actname));
    }
  } extra_type_by_cause_iterate_end;

  g_menu_remove(work_menu, 4);
  g_menu_insert_submenu(work_menu, 4, _("Build _Mine"), G_MENU_MODEL(submenu));

  submenu = g_menu_new();

  extra_type_by_rmcause_iterate(ERM_CLEAN, pextra) {
    char actname[256];
    GSimpleAction *act;

    fc_snprintf(actname, sizeof(actname), "clean_%d", i);
    act = g_simple_action_new(actname, NULL);
    g_simple_action_set_enabled(act,
                                can_units_do_activity_targeted_client(punits,
                                                                      ACTIVITY_CLEAN,
                                                                      pextra));
    g_action_map_add_action(map, G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(clean_menu_callback), pextra);

    fc_snprintf(actname, sizeof(actname), "app.clean_%d", i++);
    menu_item_append_unref(submenu,
                           g_menu_item_new(extra_name_translation(pextra), actname));
  } extra_type_by_rmcause_iterate_end;

  g_menu_remove(work_menu, 5);
  g_menu_insert_submenu(work_menu, 5, _("_Clean Nuisance"),
                        G_MENU_MODEL(submenu));

  submenu = g_menu_new();

  extra_type_by_cause_iterate(EC_BASE, pextra) {
    if (pextra->buildable) {
      char actname[256];
      GSimpleAction *act;

      fc_snprintf(actname, sizeof(actname), "base_%d", i);
      act = g_simple_action_new(actname, NULL);
      g_simple_action_set_enabled(act,
                                  can_units_do_activity_targeted_client(punits,
                                                                        ACTIVITY_BASE,
                                                                        pextra));
      g_action_map_add_action(map, G_ACTION(act));
      g_signal_connect(act, "activate", G_CALLBACK(base_callback), pextra);

      fc_snprintf(actname, sizeof(actname), "app.base_%d", i++);
      menu_item_append_unref(submenu,
                             g_menu_item_new(extra_name_translation(pextra), actname));
    }
  } extra_type_by_cause_iterate_end;

  g_menu_remove(combat_menu, 3);
  g_menu_insert_submenu(combat_menu, 3, _("Build _Base"), G_MENU_MODEL(submenu));

  bool units_all_same_tile = TRUE, units_all_same_type = TRUE;
  char acttext[128], irrtext[128], mintext[128], transtext[128];
  char cultext[128], plantext[128];
  struct terrain *pterrain;

  if (!can_client_change_view()) {
    return;
  }

  num_units = get_num_units_in_focus();

  if (num_units > 0) {
    const struct tile *ptile = NULL;
    const struct unit_type *ptype = NULL;

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

  menu_entry_group_set_sensitive(map, MGROUP_EDIT, editor_is_active());
  menu_entry_group_set_sensitive(map, MGROUP_PLAYING,
                                 can_client_issue_orders() && !editor_is_active());
  menu_entry_group_set_sensitive(map, MGROUP_UNIT,
                                 num_units > 0
                                 && can_client_issue_orders()
                                 && !editor_is_active());

  menu_entry_set_sensitive(map, "CENTER_VIEW", can_client_issue_orders());
  menu_entry_set_sensitive(map, "VOLUME_UP", TRUE);
  menu_entry_set_sensitive(map, "VOLUME_DOWN", TRUE);

  menu_entry_set_sensitive(map, "GAME_SAVE_AS",
                           can_client_access_hack() && C_S_RUNNING <= client_state());
  menu_entry_set_sensitive(map, "GAME_SAVE",
                           can_client_access_hack() && C_S_RUNNING <= client_state());

  menu_entry_set_sensitive(map, "SERVER_OPTIONS", client.conn.established);

  menu_entry_set_sensitive(map, "LEAVE",
                           client.conn.established);

  menu_entry_set_sensitive(map, "RALLY_DLG", can_client_issue_orders());
  menu_entry_set_sensitive(map, "INFRA_DLG", terrain_control.infrapoints);

  menu_entry_set_sensitive(map, "POLICIES", multiplier_count() > 0);
  menu_entry_set_sensitive(map, "REPORT_TOP_CITIES", game.info.top_cities_count > 0);
  menu_entry_set_sensitive(map, "REPORT_SPACESHIP",
                           client_player() != NULL && client_player()->spaceship.state != SSHIP_NONE);

  info = menu_entry_info_find("EDIT_MODE");
  if (info->state != game.info.is_edit_mode) {
    info->state = game.info.is_edit_mode;
    g_menu_remove(edit_menu, 4);
    menu_item_insert_unref(edit_menu, 4,
                           create_toggle_menu_item(info));

    menu_entry_set_sensitive(map, "EDIT_MODE",
                             can_conn_enable_editing(&client.conn));
    editgui_refresh();
  }

  info = menu_entry_info_find("TOGGLE_FOG");
  if (info->state != game.client.fog_of_war) {
    info->state = game.client.fog_of_war;

    g_menu_remove(edit_menu, 5);
    menu_item_insert_unref(edit_menu, 5,
                           create_toggle_menu_item(info));
  }

  {
    char road_buf[500];

    proad = road_by_gui_type(ROAD_GUI_ROAD);
    if (proad != NULL) {
      /* TRANS: Connect with some road type (Road/Railroad) */
      snprintf(road_buf, sizeof(road_buf), _("Connect With %s"),
               extra_name_translation(road_extra_get(proad)));
      menus_rename(work_menu, 12, "CONNECT_ROAD", road_buf);
    }

    proad = road_by_gui_type(ROAD_GUI_RAILROAD);
    if (proad != NULL) {
      snprintf(road_buf, sizeof(road_buf), _("Connect With %s"),
               extra_name_translation(road_extra_get(proad)));
      menus_rename(work_menu, 13, "CONNECT_RAIL", road_buf);
    }
  }

  if (!can_client_issue_orders()) {
    return;
  }

  if (!punits) {
    return;
  }

  /* Remaining part of this function: Update Unit, Work, and Combat menus */

  /* Enable the button for adding to a city in all cases, so we
   * get an eventual error message from the server if we try. */
  menu_entry_set_sensitive(map, "BUILD_CITY",
                           (can_units_do_on_map(&(wld.map), punits,
                                                unit_can_add_or_build_city)
                            || can_units_do_on_map(&(wld.map), punits,
                                                   unit_can_help_build_wonder_here)));

  menu_entry_set_sensitive(map, "DO_ACTION",
                           units_can_do_action(punits, ACTION_ANY, TRUE));

  menu_entry_set_sensitive(map, "UNIT_TELEPORT",
                           can_units_do_on_map(&(wld.map), punits, can_unit_teleport));

  menu_entry_set_sensitive(map, "BUILD_ROAD",
                           (can_units_do_any_road(&(wld.map), punits)
                            || can_units_do(punits,
                                            unit_can_est_trade_route_here)));
  menu_entry_set_sensitive(map, "BUILD_IRRIGATION",
                           can_units_do_activity_client(punits, ACTIVITY_IRRIGATE));
  menu_entry_set_sensitive(map, "BUILD_MINE",
                           can_units_do_activity_client(punits, ACTIVITY_MINE));
  menu_entry_set_sensitive(map, "CULTIVATE",
                           can_units_do_activity_client(punits, ACTIVITY_CULTIVATE));
  menu_entry_set_sensitive(map, "PLANT",
                           can_units_do_activity_client(punits, ACTIVITY_PLANT));
  menu_entry_set_sensitive(map, "TRANSFORM_TERRAIN",
                           can_units_do_activity_client(punits, ACTIVITY_TRANSFORM));
  menu_entry_set_sensitive(map, "FORTIFY",
                           can_units_do_activity_client(punits,
                                                        ACTIVITY_FORTIFYING));
  menu_entry_set_sensitive(map, "PARADROP",
                           can_units_do_on_map(&(wld.map), punits, can_unit_paradrop));
  menu_entry_set_sensitive(map, "PILLAGE",
                           can_units_do_activity_client(punits, ACTIVITY_PILLAGE));
  menu_entry_set_sensitive(map, "CLEAN",
                           can_units_do_activity_client(punits, ACTIVITY_CLEAN));
  menu_entry_set_sensitive(map, "BUILD_FORTRESS",
                           can_units_do_base_gui(punits, BASE_GUI_FORTRESS));
  menu_entry_set_sensitive(map, "BUILD_AIRBASE",
                           can_units_do_base_gui(punits, BASE_GUI_AIRBASE));
  menu_entry_set_sensitive(map, "UNIT_SENTRY",
                           can_units_do_activity_client(punits, ACTIVITY_SENTRY));
  menu_entry_set_sensitive(map, "UNSENTRY_ALL",
                           units_have_activity_on_tile(punits,
                                                       ACTIVITY_SENTRY));
  menu_entry_set_sensitive(map, "UNIT_HOMECITY",
                           can_units_do_on_map(&(wld.map), punits, can_unit_change_homecity));
  menu_entry_set_sensitive(map, "UNIT_UPGRADE", units_can_upgrade(&(wld.map), punits));
  menu_entry_set_sensitive(map, "UNIT_CONVERT", units_can_convert(&(wld.map), punits));
  menu_entry_set_sensitive(map, "UNIT_DISBAND",
                           units_can_do_action(punits, ACTION_DISBAND_UNIT,
                                               TRUE));

  menu_entry_set_sensitive(map, "AUTO_WORKER",
                           can_units_do(punits, can_unit_do_autoworker));
  menu_entry_set_sensitive(map, "UNIT_EXPLORE",
                           can_units_do_activity_client(punits, ACTIVITY_EXPLORE));
  menu_entry_set_sensitive(map, "UNIT_BOARD",
                           units_can_load(punits));
  menu_entry_set_sensitive(map, "UNIT_DEBOARD",
                           units_can_unload(&(wld.map), punits));
  menu_entry_set_sensitive(map, "UNIT_UNLOAD_TRANSPORTER",
                           units_are_occupied(punits));

  proad = road_by_gui_type(ROAD_GUI_ROAD);
  if (proad != NULL) {
    struct extra_type *tgt;

    tgt = road_extra_get(proad);

    conn_possible = can_units_do_connect(punits, ACTIVITY_GEN_ROAD, tgt);
  } else {
    conn_possible = FALSE;
  }
  menu_entry_set_sensitive(map, "CONNECT_ROAD", conn_possible);

  proad = road_by_gui_type(ROAD_GUI_RAILROAD);
  if (proad != NULL) {
    struct extra_type *tgt;

    tgt = road_extra_get(proad);

    conn_possible = can_units_do_connect(punits, ACTIVITY_GEN_ROAD, tgt);
  } else {
    conn_possible = FALSE;
  }
  menu_entry_set_sensitive(map, "CONNECT_RAIL", conn_possible);

  proad = road_by_gui_type(ROAD_GUI_MAGLEV);
  if (proad != NULL) {
    struct extra_type *tgt;

    tgt = road_extra_get(proad);

    conn_possible = can_units_do_connect(punits, ACTIVITY_GEN_ROAD, tgt);
  } else {
    conn_possible = FALSE;
  }
  menu_entry_set_sensitive(map, "CONNECT_MAGLEV", conn_possible);

  extras = extra_type_list_by_cause(EC_IRRIGATION);

  if (extra_type_list_size(extras) > 0) {
    struct extra_type *tgt;

    tgt = extra_type_list_get(extras, 0);
    conn_possible = can_units_do_connect(punits, ACTIVITY_IRRIGATE, tgt);
  } else {
    conn_possible = FALSE;
  }
  menu_entry_set_sensitive(map, "CONNECT_IRRIGATION", conn_possible);

  menu_entry_set_sensitive(map, "TAX_RATES",
                           game.info.changable_tax
                           && can_client_issue_orders());

  if (units_can_do_action(punits, ACTION_HELP_WONDER, TRUE)) {
    menus_rename(work_menu, 0, "BUILD_CITY",
                 action_get_ui_name_mnemonic(ACTION_HELP_WONDER, "_"));
  } else {
    bool city_on_tile = FALSE;

    /* FIXME: This overloading doesn't work well with multiple focus
     * units. */
    unit_list_iterate(punits, punit) {
      if (tile_city(unit_tile(punit))) {
        city_on_tile = TRUE;
        break;
      }
    } unit_list_iterate_end;

    if (city_on_tile && units_can_do_action(punits, ACTION_JOIN_CITY,
                                            TRUE)) {
      menus_rename(work_menu, 0, "BUILD_CITY",
                   action_get_ui_name_mnemonic(ACTION_JOIN_CITY, "_"));
    } else {
      /* Refresh default order */
      menus_rename(work_menu, 0, "BUILD_CITY",
                   action_get_ui_name_mnemonic(ACTION_FOUND_CITY, "_"));
    }
  }

  if (units_can_do_action(punits, ACTION_TRADE_ROUTE, TRUE)) {
    menus_rename(work_menu, 6, "BUILD_ROAD",
                 action_get_ui_name_mnemonic(ACTION_TRADE_ROUTE, "_"));
  } else if (units_have_type_flag(punits, UTYF_WORKERS, TRUE)) {
    char road_item[500];
    struct extra_type *pextra = NULL;

    /* FIXME: This overloading doesn't work well with multiple focus
     * units. */
    unit_list_iterate(punits, punit) {
      pextra = next_extra_for_tile(unit_tile(punit), EC_ROAD,
                                   unit_owner(punit), punit);
      if (pextra != NULL) {
        break;
      }
    } unit_list_iterate_end;

    if (pextra != NULL) {
      /* TRANS: Build road of specific type (Road/Railroad) */
      snprintf(road_item, sizeof(road_item), _("Build %s"),
               extra_name_translation(pextra));
      menus_rename(work_menu, 6, "BUILD_ROAD", road_item);
    }
  } else {
    menus_rename(work_menu, 6, "BUILD_ROAD", _("Build _Road"));
  }

  if (units_all_same_type && num_units > 0) {
    struct unit *punit = unit_list_get(punits, 0);
    const struct unit_type *to_unittype
      = can_upgrade_unittype(client_player(), unit_type_get(punit));

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
    menus_rename(unit_menu, 13, "UNIT_UPGRADE", acttext);
  } else {
    menus_rename(unit_menu, 13, "UNIT_UPGRADE", _("Upgr_ade"));
  }

  if (units_can_convert(&(wld.map), punits)) {
    if (units_all_same_type && num_units > 0) {
      struct unit *punit = unit_list_get(punits, 0);

      /* TRANS: %s is a unit type. */
      fc_snprintf(acttext, sizeof(acttext), _("C_onvert to %s"),
                  utype_name_translation(unit_type_get(punit)->converted_to));
    } else {
      acttext[0] = '\0';
    }
  } else {
    menu_entry_set_sensitive(map, "UNIT_CONVERT", FALSE);
    acttext[0] = '\0';
  }
  if ('\0' != acttext[0]) {
    menus_rename(unit_menu, 14, "UNIT_CONVERT", acttext);
  } else {
    menus_rename(unit_menu, 14, "UNIT_CONVERT", _("C_onvert"));
  }

  if (units_all_same_tile && num_units > 0) {
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

  menus_rename(work_menu, 7, "BUILD_IRRIGATION", irrtext);
  menus_rename(work_menu, 9, "CULTIVATE", cultext);
  menus_rename(work_menu, 8, "BUILD_MINE", mintext);
  menus_rename(work_menu, 10, "PLANT", plantext);
  menus_rename(work_menu, 11, "TRANSFORM_TERRAIN", transtext);

  menus_rename(unit_menu, 12, "UNIT_HOMECITY",
               action_get_ui_name_mnemonic(ACTION_HOME_CITY, "_"));

  menus_rename(combat_menu, 1, "BUILD_FORTRESS",
               Q_(terrain_control.gui_type_base0));
  menus_rename(combat_menu, 2, "BUILD_AIRBASE",
               Q_(terrain_control.gui_type_base1));
}

/************************************************************************//**
  Initialize menus (sensitivity, name, etc.) based on the
  current state and current ruleset, etc. Call menus_update().
****************************************************************************/
void real_menus_init(void)
{
  if (!menus_built) {
    return;
  }

#ifdef MENUS_GTK3

  if (!can_client_change_view()) {
    menu_entry_group_set_sensitive(MGROUP_ALL, FALSE);

    return;
  }

  menu_entry_group_set_sensitive(MGROUP_SAFE, TRUE);
  menu_entry_group_set_sensitive(MGROUP_PLAYER, client_has_player());

  menu_entry_set_sensitive("SHOW_NATIONAL_BORDERS",
                           BORDERS_DISABLED != game.info.borders);

  view_menu_update_sensitivity();
#endif /* MENUS_GTK3 */
}

/**********************************************************************//**
  Enable/Disable the game page menu bar.
**************************************************************************/
void enable_menus(bool enable)
{
  GtkApplication *fc_app = gui_app();

  if (enable) {
    if (main_menubar == NULL) {
      setup_app_actions(G_APPLICATION(fc_app));
      main_menubar = setup_menus(fc_app);
      /* Ensure the menus are really created before performing any operations
       * on them. */
      while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
      }
    }

    gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(toplevel), TRUE);
    gtk_application_set_menubar(fc_app, G_MENU_MODEL(main_menubar));

  } else {
    gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(toplevel), FALSE);
    gtk_application_set_menubar(fc_app, NULL);
  }

  /* Workaround for gtk bug that (re)setting the menubar after the window has
   * been already created is not noticed. */
  g_object_notify(G_OBJECT(gtk_settings_get_default()), "gtk-shell-shows-menubar");
}

/**********************************************************************//**
  Disable all unit related commands.
**************************************************************************/
void menus_disable_unit_commands(void)
{
  menu_entry_group_set_sensitive(G_ACTION_MAP(gui_app()), MGROUP_UNIT, FALSE);
}

/**********************************************************************//**
  Disable all char commands.
**************************************************************************/
void menus_disable_char_commands(void)
{
  menu_entry_group_set_sensitive(G_ACTION_MAP(gui_app()), MGROUP_CHAR, FALSE);
}
