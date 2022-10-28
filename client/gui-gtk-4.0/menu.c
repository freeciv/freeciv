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

#ifndef GTK_STOCK_EDIT
#define GTK_STOCK_EDIT NULL
#endif

static GMenu *main_menubar = NULL;
static bool menus_built = FALSE;

static GMenu *setup_menus(GtkApplication *app);

#ifndef FREECIV_DEBUG
static void menu_entry_set_visible(const char *key,
                                   gboolean is_visible,
                                   gboolean is_sensitive);
#endif /* FREECIV_DEBUG */

#ifdef MENUS_GTK3
static void menu_entry_set_active(const char *key,
                                  gboolean is_active);
static void view_menu_update_sensitivity(void);
#endif /* MENUS_GTK3 */

enum menu_entry_grouping { MGROUP_SAFE, MGROUP_EDIT, MGROUP_PLAYING,
                           MGROUP_UNIT, MGROUP_PLAYER, MGROUP_ALL };

static GMenu *gov_menu = NULL;
static GMenu *unit_menu = NULL;
static GMenu *work_menu = NULL;
static GMenu *combat_menu = NULL;

struct menu_entry_info {
  const char *key;
  const char *name;
  const char *action;
  const char *accl;
#ifdef MENUS_GTK3
  guint accel;
  GdkModifierType accel_mod;
  GCallback cb;
#endif /* MENUS_GTK3 */
  enum menu_entry_grouping grouping;
};

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

#ifdef MENUS_GTK3
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
#endif /* MENUS_GTK3 */

static void leave_callback(GSimpleAction *action,
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

#ifdef MENUS_GTK3
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
static void help_nations_callback(GtkMenuItem *item, gpointer data);
static void help_connecting_callback(GtkMenuItem *item, gpointer data);
static void help_controls_callback(GtkMenuItem *item, gpointer data);
static void help_cma_callback(GtkMenuItem *item, gpointer data);
static void help_chatline_callback(GtkMenuItem *item, gpointer data);
static void help_worklist_editor_callback(GtkMenuItem *item, gpointer data);
static void help_language_callback(GtkMenuItem *item, gpointer data);
#endif /* MENUS_GTK3 */

static void help_copying_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void help_about_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);

#ifdef MENUS_GTK3
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
static void show_road_rails_callback(GtkCheckMenuItem *item, gpointer data);
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
static void show_focus_unit_callback(GtkCheckMenuItem *item, gpointer data);
static void show_fog_of_war_callback(GtkCheckMenuItem *item, gpointer data);
static void full_screen_callback(GtkCheckMenuItem *item, gpointer data);
static void recalc_borders_callback(GtkMenuItem *item, gpointer data);
static void toggle_fog_callback(GtkMenuItem *item, gpointer data);
static void scenario_properties_callback(GtkMenuItem *item, gpointer data);
static void save_scenario_callback(GtkMenuItem *item, gpointer data);
#endif /* MENUS_GTK3 */

static void center_view_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data);
static void report_economy_callback(GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer data);
static void report_research_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data);

#ifdef MENUS_GTK3
static void multiplier_callback(GtkMenuItem *item, gpointer data);
static void report_spaceship_callback(GtkMenuItem *item, gpointer data);
static void report_achievements_callback(GtkMenuItem *item, gpointer data);
#endif /* MENUS_GTK3 */

static void government_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void revolution_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);

#ifdef MENUS_GTK3
static void tax_rate_callback(GtkMenuItem *item, gpointer data);
static void select_single_callback(GtkMenuItem *item, gpointer data);
static void select_all_on_tile_callback(GtkMenuItem *item, gpointer data);
static void select_same_type_tile_callback(GtkMenuItem *item, gpointer data);
static void select_same_type_cont_callback(GtkMenuItem *item, gpointer data);
static void select_same_type_callback(GtkMenuItem *item, gpointer data);
static void select_dialog_callback(GtkMenuItem *item, gpointer data);
static void rally_dialog_callback(GtkMenuItem *item, gpointer data);
#endif /* MENUS_GTK3 */

static void infra_dialog_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data);
static void unit_wait_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);
static void unit_done_callback(GSimpleAction *action,
                               GVariant *parameter,
                               gpointer data);

#ifdef MENUS_GTK3
static void unit_goto_callback(GtkMenuItem *item, gpointer data);
static void unit_goto_city_callback(GtkMenuItem *item, gpointer data);
static void unit_return_callback(GtkMenuItem *item, gpointer data);
#endif /* MENUS_GTK3 */

static void unit_explore_callback(GSimpleAction *action,
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

#ifdef MENUS_GTK3
static void unit_patrol_callback(GtkMenuItem *item, gpointer data);
static void unit_unsentry_callback(GtkMenuItem *item, gpointer data);
static void unit_board_callback(GtkMenuItem *item, gpointer data);
static void unit_deboard_callback(GtkMenuItem *item, gpointer data);
static void unit_unload_transporter_callback(GtkMenuItem *item,
                                             gpointer data);
#endif /* MENUS_GTK3 */

static void build_city_callback(GSimpleAction *action,
                                GVariant *parameter,
                                gpointer data);
static void auto_settle_callback(GSimpleAction *action,
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

#ifdef MENUS_GTK3
static void build_road_callback(GtkMenuItem *item, gpointer data);
static void build_irrigation_callback(GtkMenuItem *item, gpointer data);
static void build_mine_callback(GtkMenuItem *item, gpointer data);
static void connect_road_callback(GtkMenuItem *item, gpointer data);
static void connect_rail_callback(GtkMenuItem *item, gpointer data);
static void connect_irrigation_callback(GtkMenuItem *item, gpointer data);
static void transform_terrain_callback(GtkMenuItem *item, gpointer data);
static void clean_pollution_callback(GtkMenuItem *item, gpointer data);
static void clean_fallout_callback(GtkMenuItem *item, gpointer data);
static void build_fortress_callback(GtkMenuItem *item, gpointer data);
static void build_airbase_callback(GtkMenuItem *item, gpointer data);
static void diplomat_action_callback(GtkMenuItem *item, gpointer data);
#endif /* MENUS_GTK3 */

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
    "clear_chat", NULL, MGROUP_SAFE },
  { "SAVE_CHAT_LOGS", N_("Save Chat _Log"),
    "save_chat", NULL, MGROUP_SAFE, },
  { "LOCAL_OPTIONS", N_("_Local Client"),
    "local_options", NULL, MGROUP_SAFE },
  { "MESSAGE_OPTIONS", N_("_Message"),
    "message_options", NULL, MGROUP_SAFE },

  { "LEAVE", N_("_Leave"),
    "leave", NULL, MGROUP_SAFE },
  { "QUIT", N_("_Quit"),
    "quit", "<ctrl>q", MGROUP_SAFE },

  /* Edit menu */
  { "INFRA_DLG", N_("Infra dialog"),
    "infra_dlg", "<ctrl>i", MGROUP_SAFE },

  /* View menu */
  { "CENTER_VIEW", N_("_Center View"),
    "center_view", "c", MGROUP_PLAYER },

  /* Unit menu */
  { "UNIT_EXPLORE", N_("Auto E_xplore"),
    "explore", "x", MGROUP_UNIT },
  { "UNIT_SENTRY", N_("_Sentry"),
    "sentry", "s", MGROUP_UNIT },
  { "UNIT_HOMECITY", N_("Set _Home City"),
    "homecity", "h", MGROUP_UNIT },
  { "UNIT_UPGRADE", N_("Upgr_ade"),
    "upgrade", "<shift>u", MGROUP_UNIT },
  { "UNIT_CONVERT", N_("C_onvert"),
    "convert", "<shift>o", MGROUP_UNIT },
  { "UNIT_DISBAND", N_("_Disband"),
    "disband", "<shift>d", MGROUP_UNIT },
  { "UNIT_WAIT", N_("_Wait"),
    "wait", "w", MGROUP_UNIT },
  { "UNIT_DONE", N_("_Done"),
    "done", "space", MGROUP_UNIT },

  /* Work menu */
  { "BUILD_CITY", N_("_Build City"),
    "build_city", "b", MGROUP_UNIT },
  { "AUTO_SETTLER", N_("_Auto Settler"),
    "auto_settle", "a", MGROUP_UNIT },
  { "CULTIVATE", N_("Cultivate"),
    "cultivate", "<shift>i", MGROUP_UNIT },
  { "PLANT", N_("Plant"),
    "plant", "<shift>m", MGROUP_UNIT },

  /* Combat menu */
  { "FORTIFY", N_("Fortify"),
    "fortify", "f", MGROUP_UNIT },
  { "PARADROP", N_("P_aradrop"),
    "paradrop", "j", MGROUP_UNIT },
  { "PILLAGE", N_("_Pillage"),
    "pillage", "<shift>p", MGROUP_UNIT },

  /* Civilization */
  { "MAP_VIEW", N_("?noun:_View"),
    "map_view", "F1", MGROUP_SAFE },
  { "REPORT_UNITS", N_("_Units"),
    "report_units", "F2", MGROUP_SAFE },
  { "REPORT_NATIONS", N_("_Nations"),
    "report_nations", "F3", MGROUP_SAFE },
  { "REPORT_CITIES", N_("_Cities"),
    "report_cities", "F4", MGROUP_SAFE },
  { "REPORT_ECONOMY", N_("_Economy"),
    "report_economy", "F5", MGROUP_PLAYER },
  { "REPORT_RESEARCH", N_("_Research"),
    "report_research", "F6", MGROUP_PLAYER },

  { "START_REVOLUTION", N_("_Revolution..."),
    "revolution", "<shift><ctrl>r", MGROUP_PLAYING },

  { "REPORT_WOW", N_("_Wonders of the World"),
    "report_wow", "F7", MGROUP_SAFE },
  { "REPORT_TOP_CITIES", N_("Top _Five Cities"),
    "report_top_cities", "F8", MGROUP_SAFE },
  { "REPORT_MESSAGES", N_("_Messages"),
    "report_messages", "F9", MGROUP_SAFE },
  { "REPORT_DEMOGRAPHIC", N_("_Demographics"),
    "report_demographics", "F11", MGROUP_SAFE },

  /* Battle Groups menu */
  /* Note that user view: 1 - 4, internal: 0 - 3 */
  { "BG_SELECT_1", N_("Select Battle Group 1"),
    "bg_select_0", "<shift>F1", MGROUP_PLAYING },
  { "BG_ASSIGN_1", N_("Assign Battle Group 1"),
    "bg_assign_0", "<ctrl>F1", MGROUP_PLAYING },
  { "BG_APPEND_1", N_("Append to Battle Group 1"),
    "bg_append_0", "<shift><ctrl>F1", MGROUP_PLAYING },
  { "BG_SELECT_2", N_("Select Battle Group 2"),
    "bg_select_1", "<shift>F2", MGROUP_PLAYING },
  { "BG_ASSIGN_2", N_("Assign Battle Group 2"),
    "bg_assign_1", "<ctrl>F2", MGROUP_PLAYING },
  { "BG_APPEND_2", N_("Append to Battle Group 2"),
    "bg_append_1", "<shift><ctrl>F2", MGROUP_PLAYING },
  { "BG_SELECT_3", N_("Select Battle Group 3"),
    "bg_select_2", "<shift>F3", MGROUP_PLAYING },
  { "BG_ASSIGN_3", N_("Assign Battle Group 3"),
    "bg_assign_2", "<ctrl>F3", MGROUP_PLAYING },
  { "BG_APPEND_3", N_("Append to Battle Group 3"),
    "bg_append_2", "<shift><ctrl>F3", MGROUP_PLAYING },
  { "BG_SELECT_4", N_("Select Battle Group 4"),
    "bg_select_3", "<shift>F4", MGROUP_PLAYING },
  { "BG_ASSIGN_4", N_("Assign Battle Group 4"),
    "bg_assign_3", "<ctrl>F4", MGROUP_PLAYING },
  { "BG_APPEND_4", N_("Append to Battle Group 4"),
    "bg_append_3", "<shift><ctrl>F4", MGROUP_PLAYING },

  /* Help menu */
  { "HELP_COPYING", N_("Copying"),
    "help_copying", NULL, MGROUP_SAFE },
  { "HELP_ABOUT", N_("About Freeciv"),
    "help_about", NULL, MGROUP_SAFE },

#ifdef MENUS_GTK3
  { "MENU_GAME", N_("_Game"), 0, 0, NULL, MGROUP_SAFE },
  { "MENU_OPTIONS", N_("_Options"), 0, 0, NULL, MGROUP_SAFE },
  { "MENU_EDIT", N_("_Edit"), 0, 0, NULL, MGROUP_SAFE },
  { "MENU_VIEW", N_("?verb:_View"), 0, 0, NULL, MGROUP_SAFE },
  { "MENU_IMPROVEMENTS", N_("_Improvements"), 0, 0,
    NULL, MGROUP_SAFE },
  { "MENU_CIVILIZATION", N_("C_ivilization"), 0, 0,
    NULL, MGROUP_SAFE },
  { "MENU_HELP", N_("_Help"), 0, 0, NULL, MGROUP_SAFE },
  { "SERVER_OPTIONS", N_("_Remote Server"), 0, 0,
    G_CALLBACK(server_options_callback), MGROUP_SAFE },
  { "SAVE_OPTIONS", N_("Save Options _Now"), 0, 0,
    G_CALLBACK(save_options_callback), MGROUP_SAFE },
  { "RELOAD_TILESET", N_("_Reload Tileset"),
    GDK_KEY_r, GDK_ALT_MASK | GDK_CONTROL_MASK,
    G_CALLBACK(reload_tileset_callback), MGROUP_SAFE },
  { "GAME_SAVE", N_("_Save Game"), GDK_KEY_s, GDK_CONTROL_MASK,
    G_CALLBACK(save_game_callback), MGROUP_SAFE },
  { "GAME_SAVE_AS", N_("Save Game _As..."), 0, 0,
    G_CALLBACK(save_game_as_callback), MGROUP_SAFE },
  { "MAPIMG_SAVE", N_("Save Map _Image"), 0, 0,
    G_CALLBACK(save_mapimg_callback), MGROUP_SAFE },
  { "MAPIMG_SAVE_AS", N_("Save _Map Image As..."), 0, 0,
    G_CALLBACK(save_mapimg_as_callback), MGROUP_SAFE },

  { "FIND_CITY", N_("_Find City"), GDK_KEY_f, GDK_CONTROL_MASK,
    G_CALLBACK(find_city_callback), MGROUP_SAFE },
  { "WORKLISTS", N_("Work_lists"), GDK_KEY_l, GDK_CONTROL_MASK,
    G_CALLBACK(worklists_callback), MGROUP_SAFE },
  { "RALLY_DLG", N_("Rally point dialog"), GDK_KEY_r, GDK_CONTROL_MASK,
    G_CALLBACK(rally_dialog_callback), MGROUP_SAFE },

  { "CLIENT_LUA_SCRIPT", N_("Client _Lua Script"), 0, 0,
    G_CALLBACK(client_lua_script_callback), MGROUP_SAFE },
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

  { "SAVE_OPTIONS_ON_EXIT", N_("Save Options on _Exit"), 0, 0,
    G_CALLBACK(save_options_on_exit_callback), MGROUP_SAFE },
  { "EDIT_MODE", N_("_Editing Mode"), GDK_KEY_e, GDK_CONTROL_MASK,
    G_CALLBACK(edit_mode_callback), MGROUP_SAFE },
  { "SHOW_CITY_OUTLINES", N_("Cit_y Outlines"), GDK_KEY_y, GDK_CONTROL_MASK,
    G_CALLBACK(show_city_outlines_callback), MGROUP_SAFE },
  { "SHOW_CITY_OUTPUT", N_("City Output"), GDK_KEY_v, GDK_CONTROL_MASK,
    G_CALLBACK(show_city_output_callback), MGROUP_SAFE },
  { "SHOW_MAP_GRID", N_("Map _Grid"), GDK_KEY_g, GDK_CONTROL_MASK,
    G_CALLBACK(show_map_grid_callback), MGROUP_SAFE },
  { "SHOW_NATIONAL_BORDERS", N_("National _Borders"), GDK_KEY_b, GDK_CONTROL_MASK,
    G_CALLBACK(show_national_borders_callback), MGROUP_SAFE },
  { "SHOW_NATIVE_TILES", N_("Native Tiles"), GDK_KEY_n, GDK_CONTROL_MASK | GDK_SHIFT_MASK,
    G_CALLBACK(show_native_tiles_callback), MGROUP_SAFE },
  { "SHOW_CITY_FULL_BAR", N_("City Full Bar"), 0, 0,
    G_CALLBACK(show_city_full_bar_callback), MGROUP_SAFE },
  { "SHOW_CITY_NAMES", N_("City _Names"), GDK_KEY_n, GDK_CONTROL_MASK,
    G_CALLBACK(show_city_names_callback), MGROUP_SAFE },
  { "SHOW_CITY_GROWTH", N_("City G_rowth"), GDK_KEY_o, GDK_CONTROL_MASK,
    G_CALLBACK(show_city_growth_callback), MGROUP_SAFE },
  { "SHOW_CITY_PRODUCTIONS", N_("City _Production Levels"), GDK_KEY_p, GDK_CONTROL_MASK,
    G_CALLBACK(show_city_productions_callback), MGROUP_SAFE },
  { "SHOW_CITY_BUY_COST", N_("City Buy Cost"), 0, 0,
    G_CALLBACK(show_city_buy_cost_callback), MGROUP_SAFE },
  { "SHOW_CITY_TRADE_ROUTES", N_("City Tra_deroutes"), GDK_KEY_d, GDK_CONTROL_MASK,
    G_CALLBACK(show_city_trade_routes_callback), MGROUP_SAFE },
  { "SHOW_TERRAIN", N_("_Terrain"), 0, 0,
    G_CALLBACK(show_terrain_callback), MGROUP_SAFE },
  { "SHOW_COASTLINE", N_("C_oastline"), 0, 0,
    G_CALLBACK(show_coastline_callback), MGROUP_SAFE },
  { "SHOW_PATHS", N_("_Paths"), 0, 0,
    G_CALLBACK(show_road_rails_callback), MGROUP_SAFE },
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
  { "SHOW_FOCUS_UNIT", N_("Focu_s Unit"), 0, 0,
    G_CALLBACK(show_focus_unit_callback), MGROUP_SAFE },
  { "SHOW_FOG_OF_WAR", N_("Fog of _War"), 0, 0,
    G_CALLBACK(show_fog_of_war_callback), MGROUP_SAFE },
  { "FULL_SCREEN", N_("_Fullscreen"), GDK_KEY_Return, GDK_ALT_MASK,
    G_CALLBACK(full_screen_callback), MGROUP_SAFE },

  { "RECALC_BORDERS", N_("Recalculate _Borders"), 0, 0,
    G_CALLBACK(recalc_borders_callback), MGROUP_EDIT },
  { "TOGGLE_FOG", N_("Toggle Fog of _War"), GDK_KEY_w,
    GDK_CONTROL_MASK | GDK_SHIFT_MASK,
    G_CALLBACK(toggle_fog_callback), MGROUP_EDIT },
  { "SCENARIO_PROPERTIES", N_("Game/Scenario Properties"), 0, 0,
    G_CALLBACK(scenario_properties_callback), MGROUP_EDIT },
  { "SAVE_SCENARIO", N_("Save Scenario"), 0, 0,
    G_CALLBACK(save_scenario_callback), MGROUP_EDIT },

  { "POLICIES", N_("_Policies..."),
    GDK_KEY_p, GDK_SHIFT_MASK | GDK_CONTROL_MASK,
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
  { "UNIT_GOTO", N_("_Go to"), GDK_KEY_g, 0,
    G_CALLBACK(unit_goto_callback), MGROUP_UNIT },
  { "MENU_GOTO_AND", N_("Go to a_nd..."), 0, 0, NULL, MGROUP_UNIT },
  { "UNIT_GOTO_CITY", N_("Go _to/Airlift to City..."), GDK_KEY_t, 0,
    G_CALLBACK(unit_goto_city_callback), MGROUP_UNIT },
  { "UNIT_RETURN", N_("_Return to Nearest City"), GDK_KEY_g, GDK_SHIFT_MASK,
    G_CALLBACK(unit_return_callback), MGROUP_UNIT },
  { "UNIT_PATROL", N_("_Patrol"), GDK_KEY_q, 0,
    G_CALLBACK(unit_patrol_callback), MGROUP_UNIT },
  { "UNIT_UNSENTRY", N_("Uns_entry All On Tile"), GDK_KEY_s, GDK_SHIFT_MASK,
    G_CALLBACK(unit_unsentry_callback), MGROUP_UNIT },
  { "UNIT_BOARD", N_("_Load"), GDK_KEY_l, 0,
    G_CALLBACK(unit_board_callback), MGROUP_UNIT },
  { "UNIT_DEBOARD", N_("_Unload"), GDK_KEY_u, 0,
    G_CALLBACK(unit_deboard_callback), MGROUP_UNIT },
  { "UNIT_UNLOAD_TRANSPORTER", N_("U_nload All From Transporter"),
    GDK_KEY_t, GDK_SHIFT_MASK,
    G_CALLBACK(unit_unload_transporter_callback), MGROUP_UNIT },

  { "BUILD_ROAD", N_("Build _Road"), GDK_KEY_r, 0,
    G_CALLBACK(build_road_callback), MGROUP_UNIT },
  { "BUILD_IRRIGATION", N_("Build _Irrigation"), GDK_KEY_i, 0,
    G_CALLBACK(build_irrigation_callback), MGROUP_UNIT },
  { "BUILD_MINE", N_("Build _Mine"), GDK_KEY_m, 0,
    G_CALLBACK(build_mine_callback), MGROUP_UNIT },
  { "CONNECT_ROAD", N_("Connect With Roa_d"), GDK_KEY_r, GDK_CONTROL_MASK,
    G_CALLBACK(connect_road_callback), MGROUP_UNIT },
  { "CONNECT_RAIL", N_("Connect With Rai_l"), GDK_KEY_l, GDK_CONTROL_MASK,
    G_CALLBACK(connect_rail_callback), MGROUP_UNIT },
  { "CONNECT_IRRIGATION", N_("Connect With Irri_gation"),
    GDK_KEY_i, GDK_CONTROL_MASK,
    G_CALLBACK(connect_irrigation_callback), MGROUP_UNIT },
  { "TRANSFORM_TERRAIN", N_("Transf_orm Terrain"), GDK_KEY_o, 0,
    G_CALLBACK(transform_terrain_callback), MGROUP_UNIT },
  { "CLEAN_POLLUTION", N_("Clean _Pollution"), GDK_KEY_p, 0,
    G_CALLBACK(clean_pollution_callback), MGROUP_UNIT },
  { "CLEAN_FALLOUT", N_("Clean _Nuclear Fallout"), GDK_KEY_n, 0,
    G_CALLBACK(clean_fallout_callback), MGROUP_UNIT },
  { "BUILD_FORTRESS", N_("Build Fortress"), GDK_KEY_f, GDK_SHIFT_MASK,
    G_CALLBACK(build_fortress_callback), MGROUP_UNIT },
  { "BUILD_AIRBASE", N_("Build Airbase"), GDK_KEY_e, GDK_SHIFT_MASK,
    G_CALLBACK(build_airbase_callback), MGROUP_UNIT },
  { "DIPLOMAT_ACTION", N_("_Do..."), GDK_KEY_d, 0,
    G_CALLBACK(diplomat_action_callback), MGROUP_UNIT },

  { "MENU_GOVERNMENT", N_("_Government"), 0, 0,
    NULL, MGROUP_PLAYING },
  { "TAX_RATE", N_("_Tax Rates..."), GDK_KEY_t, GDK_CONTROL_MASK,
    G_CALLBACK(tax_rate_callback), MGROUP_PLAYING },

#endif /* MENUS_GTK3 */

  { NULL }
};

const GActionEntry acts[] = {
  { "clear_chat", clear_chat_logs_callback },
  { "save_chat", save_chat_logs_callback },
  { "local_options", local_options_callback },
  { "message_options", message_options_callback },

  { "leave", leave_callback },
  { "quit", quit_callback },

  { "infra_dlg", infra_dialog_callback },

  { "center_view", center_view_callback },

  { "explore", unit_explore_callback },
  { "sentry", unit_sentry_callback },
  { "homecity", unit_homecity_callback },
  { "upgrade", unit_upgrade_callback },
  { "convert", unit_convert_callback },
  { "disband", unit_disband_callback },
  { "wait", unit_wait_callback },
  { "done", unit_done_callback },

  { "build_city", build_city_callback },
  { "auto_settle", auto_settle_callback },
  { "cultivate", cultivate_callback },
  { "plant", plant_callback },

  { "fortify", fortify_callback },
  { "paradrop", paradrop_callback },
  { "pillage", pillage_callback },

  { "revolution", revolution_callback },

  { "map_view", map_view_callback },
  { "report_units", report_units_callback },
  { "report_nations", report_nations_callback },
  { "report_cities", report_cities_callback },
  { "report_economy", report_economy_callback },
  { "report_research", report_research_callback },
  { "report_wow", report_wow_callback },
  { "report_top_cities", report_top_cities_callback },
  { "report_messages", report_messages_callback },
  { "report_demographics", report_demographic_callback },

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

#ifdef MENUS_GTK3
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
#endif /* MENUS_GTK3 */

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
      disconnect_from_server();
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
    GtkWidget* dialog =
        gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_WARNING,
                               GTK_BUTTONS_OK_CANCEL,
                               _("Leaving a local game will end it!"));
    setup_dialog(dialog, toplevel);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(leave_local_game_response), NULL);
    gtk_window_present(GTK_WINDOW(dialog));
  } else {
    disconnect_from_server();
  }
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

#ifdef MENUS_GTK3
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
#endif /* MENUS_GTK3 */

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
  send_report_request(REPORT_WONDERS_OF_THE_WORLD);
}

/************************************************************************//**
  Item "REPORT_TOP_CITIES" callback.
****************************************************************************/
static void report_top_cities_callback(GSimpleAction *action,
                                       GVariant *parameter,
                                       gpointer data)
{
  send_report_request(REPORT_TOP_5_CITIES);
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

#ifdef MENUS_GTK3
/************************************************************************//**
  Item "CLIENT_LUA_SCRIPT" callback.
****************************************************************************/
static void client_lua_script_callback(GtkMenuItem *item, gpointer data)
{
  luaconsole_dialog_popup(TRUE);
}
#endif /* MENUS_GTK3 */

/************************************************************************//**
  Item "REPORT_DEMOGRAPHIC" callback.
****************************************************************************/
static void report_demographic_callback(GSimpleAction *action,
                                        GVariant *parameter,
                                        gpointer data)
{
  send_report_request(REPORT_DEMOGRAPHIC);
}

#ifdef MENUS_GTK3
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
#endif /* MENUS_GTK3 */

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

#ifdef MENUS_GTK3
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
    /* Unreachbale techs in reqtree on/off */
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
  Item "SHOW_ROAD_RAILS" callback.
****************************************************************************/
static void show_road_rails_callback(GtkCheckMenuItem *item, gpointer data)
{
  if (gui_options.draw_roads_rails ^ gtk_check_menu_item_get_active(item)) {
    key_roads_rails_toggle();
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

    if (GUI_GTK_OPTION(fullscreen)) {
      gtk_window_fullscreen(GTK_WINDOW(toplevel));
    } else {
      gtk_window_unfullscreen(GTK_WINDOW(toplevel));
    }
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
static void toggle_fog_callback(GtkMenuItem *item, gpointer data)
{
  key_editor_toggle_fogofwar();
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
#endif /* MENUS_GTK3 */

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

#ifdef MENUS_GTK3
/************************************************************************//**
  Item "UNIT_GOTO" callback.
****************************************************************************/
static void unit_goto_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_goto();
}
#endif /* MENUS_GTK3 */

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

#ifdef MENUS_GTK3
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
#endif /* MENUS_GTK3 */

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
  Item "UNIT_SENTRY" callback.
****************************************************************************/
static void unit_sentry_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data)
{
  key_unit_sentry();
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

#ifdef MENUS_GTK3
/************************************************************************//**
  Item "UNIT_PATROL" callback.
****************************************************************************/
static void unit_patrol_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_patrol();
}

/************************************************************************//**
  Item "UNIT_UNSENTRY" callback.
****************************************************************************/
static void unit_unsentry_callback(GtkMenuItem *item, gpointer data)
{
  key_unit_wakeup_others();
}

/************************************************************************//**
  Item "UNIT_BORAD" callback.
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
#endif /* MENUS_GTK3 */

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
    if (unit_can_add_or_build_city(punit)) {
      request_unit_build_city(punit);
    } else if (utype_can_do_action(unit_type_get(punit),
                                   ACTION_HELP_WONDER)) {
      request_unit_caravan_action(punit, ACTION_HELP_WONDER);
    }
  } unit_list_iterate_end;
}

/************************************************************************//**
  Action "AUTO_SETTLE" callback.
****************************************************************************/
static void auto_settle_callback(GSimpleAction *action,
                                 GVariant *parameter,
                                 gpointer data)
{
  key_unit_auto_settle();
}

#ifdef MENUS_GTK3
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
        && can_unit_do_activity_targeted(punit, ACTIVITY_GEN_ROAD, tgt)) {
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
#endif /* MENUS_GTK3 */

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

#ifdef MENUS_GTK3
/************************************************************************//**
  Action "BUILD_MINE" callback.
****************************************************************************/
static void build_mine_callback(GtkMenuItem *action, gpointer data)
{
  key_unit_mine();
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
  Action "CLEAN_POLLUTION" callback.
****************************************************************************/
static void clean_pollution_callback(GtkMenuItem *action, gpointer data)
{
  unit_list_iterate(get_units_in_focus(), punit) {
    struct extra_type *pextra;

    pextra = prev_extra_in_tile(unit_tile(punit), ERM_CLEANPOLLUTION,
                                unit_owner(punit), punit);

    if (pextra != NULL) {
      request_new_unit_activity_targeted(punit, ACTIVITY_POLLUTION, pextra);
    }
  } unit_list_iterate_end;
}

/************************************************************************//**
  Action "CLEAN_FALLOUT" callback.
****************************************************************************/
static void clean_fallout_callback(GtkMenuItem *action, gpointer data)
{
  key_unit_fallout();
}

/************************************************************************//**
  Action "BUILD_FORTRESS" callback.
****************************************************************************/
static void build_fortress_callback(GtkMenuItem *action, gpointer data)
{
  key_unit_fortress();
}

/************************************************************************//**
  Action "BUILD_AIRBASE" callback.
****************************************************************************/
static void build_airbase_callback(GtkMenuItem *action, gpointer data)
{
  key_unit_airbase();
}
#endif /* MENUS_GTK3 */

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

#ifdef MENUS_GTK3
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
#endif /* MENUS_GTK3 */

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
static void clean_callback(GSimpleAction *action,
                           GVariant *parameter,
                           gpointer data)
{
  struct extra_type *pextra = data;

  unit_list_iterate(get_units_in_focus(), punit) {
    if (can_unit_do_activity_targeted(punit, ACTIVITY_POLLUTION, pextra)) {
      request_new_unit_activity_targeted(punit, ACTIVITY_POLLUTION,
                                         pextra);
    } else {
      request_new_unit_activity_targeted(punit, ACTIVITY_FALLOUT,
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

#ifdef MENUS_GTK3
/************************************************************************//**
  Action "REPORT_SPACESHIP" callback.
****************************************************************************/
static void report_spaceship_callback(GtkMenuItem *action, gpointer data)
{
  if (NULL != client.conn.playing) {
    popup_spaceship_dialog(client.conn.playing);
  }
}
#endif /* MENUS_GTK3 */

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
  Set name of the menu item.
****************************************************************************/
static void menu_entry_init(GMenu *sub, const char *key)
{
  struct menu_entry_info *info = menu_entry_info_find(key);

  if (info != NULL) {
    GMenuItem *item;
    char actname[150];

    fc_snprintf(actname, sizeof(actname), "app.%s", info->action);
    item = g_menu_item_new(Q_(info->name), actname);
    g_menu_append_item(sub, item);
  }
}

/************************************************************************//**
  Registers menu actions for the application.
****************************************************************************/
void setup_app_actions(GApplication *fc_app)
{
  g_action_map_add_action_entries(G_ACTION_MAP(fc_app), acts,
                                  G_N_ELEMENTS(acts), fc_app);
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

  submenu = g_menu_new();
  menu_entry_init(submenu, "LOCAL_OPTIONS");
  menu_entry_init(submenu, "MESSAGE_OPTIONS");

  g_menu_append_submenu(topmenu, _("_Options"), G_MENU_MODEL(submenu));

  menu_entry_init(topmenu, "LEAVE");
  menu_entry_init(topmenu, "QUIT");

  g_menu_append_submenu(menubar, _("_Game"), G_MENU_MODEL(topmenu));

  topmenu = g_menu_new();

  menu_entry_init(topmenu, "INFRA_DLG");

  g_menu_append_submenu(menubar, _("_Edit"), G_MENU_MODEL(topmenu));

  topmenu = g_menu_new();

  menu_entry_init(topmenu, "CENTER_VIEW");

  g_menu_append_submenu(menubar, Q_("?verb:_View"), G_MENU_MODEL(topmenu));

  unit_menu = g_menu_new();

  /* Placeholder submenu (so that menu update has something to replace) */
  submenu = g_menu_new();
  g_menu_append_submenu(unit_menu, N_("Go to a_nd..."), G_MENU_MODEL(submenu));

  menu_entry_init(unit_menu, "UNIT_EXPLORE");
  menu_entry_init(unit_menu, "UNIT_SENTRY");
  menu_entry_init(unit_menu, "UNIT_HOMECITY");
  menu_entry_init(unit_menu, "UNIT_UPGRADE");
  menu_entry_init(unit_menu, "UNIT_CONVERT");
  menu_entry_init(unit_menu, "UNIT_DISBAND");
  menu_entry_init(unit_menu, "UNIT_WAIT");
  menu_entry_init(unit_menu, "UNIT_DONE");

  g_menu_append_submenu(menubar, _("_Unit"), G_MENU_MODEL(unit_menu));

  work_menu = g_menu_new();
  menu_entry_init(work_menu, "BUILD_CITY");
  menu_entry_init(work_menu, "AUTO_SETTLER");

  /* Placeholder submenus (so that menu update has something to replace) */
  submenu = g_menu_new();
  g_menu_append_submenu(work_menu, _("Build _Path"), G_MENU_MODEL(submenu));
  submenu = g_menu_new();
  g_menu_append_submenu(work_menu, _("Build _Irrigation"), G_MENU_MODEL(submenu));
  submenu = g_menu_new();
  g_menu_append_submenu(work_menu, _("Build _Mine"), G_MENU_MODEL(submenu));
  submenu = g_menu_new();
  g_menu_append_submenu(work_menu, _("_Clean"), G_MENU_MODEL(submenu));

  menu_entry_init(work_menu, "CULTIVATE");
  menu_entry_init(work_menu, "PLANT");

  g_menu_append_submenu(menubar, _("_Work"), G_MENU_MODEL(work_menu));

  combat_menu = g_menu_new();
  menu_entry_init(combat_menu, "FORTIFY");

  /* Placeholder submenu (so that menu update has something to replace) */
  submenu = g_menu_new();
  g_menu_append_submenu(combat_menu, _("Build _Base"), G_MENU_MODEL(submenu));

  g_menu_append_submenu(menubar, _("_Combat"), G_MENU_MODEL(combat_menu));

  menu_entry_init(combat_menu, "PARADROP");
  menu_entry_init(combat_menu, "PILLAGE");

  gov_menu = g_menu_new();

  menu_entry_init(gov_menu, "MAP_VIEW");
  menu_entry_init(gov_menu, "REPORT_UNITS");
  menu_entry_init(gov_menu, "REPORT_NATIONS");
  menu_entry_init(gov_menu, "REPORT_CITIES");
  menu_entry_init(gov_menu, "REPORT_ECONOMY");
  menu_entry_init(gov_menu, "REPORT_RESEARCH");

  /* Placeholder submenu (so that menu update has something to replace) */
  submenu = g_menu_new();
  g_menu_append_submenu(gov_menu, _("_Government"), G_MENU_MODEL(submenu));

  menu_entry_init(gov_menu, "REPORT_WOW");
  menu_entry_init(gov_menu, "REPORT_TOP_CITIES");
  menu_entry_init(gov_menu, "REPORT_MESSAGES");
  menu_entry_init(gov_menu, "REPORT_DEMOGRAPHIC");

  g_menu_append_submenu(menubar, _("C_ivilization"), G_MENU_MODEL(gov_menu));

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

  g_menu_append_submenu(menubar, _("Battle Groups"), G_MENU_MODEL(topmenu));

  topmenu = g_menu_new();

  menu_entry_init(topmenu, "HELP_COPYING");
  menu_entry_init(topmenu, "HELP_ABOUT");

  g_menu_append_submenu(menubar, N_("_Help"), G_MENU_MODEL(topmenu));

#ifndef FREECIV_DEBUG
  menu_entry_set_visible("RELOAD_TILESET", FALSE, FALSE);
#endif /* FREECIV_DEBUG */

  for (i = 0; menu_entries[i].key != NULL; i++) {
    if (menu_entries[i].accl != NULL) {
      const char *accls[2] = { menu_entries[i].accl, NULL };
      char actname[150];

      fc_snprintf(actname, sizeof(actname), "app.%s", menu_entries[i].action);
      gtk_application_set_accels_for_action(app, actname, accls);
    }
  }

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

#ifdef MENUS_GTK3
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
#endif /* MENUS_GTK3 */

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
    if (menu_entries[i].grouping == group || group == MGROUP_ALL) {
      menu_entry_set_sensitive_info(map, &(menu_entries[i]), is_enabled);
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
#ifdef MENUS_GTK3
  GtkWidget *item = GTK_WIDGET(gtk_builder_get_object(ui_builder, key));

  if (item != NULL) {
    gtk_widget_set_visible(item, is_visible);
    gtk_widget_set_sensitive(item, is_sensitive);
  }
#endif /* MENUS_GTK3 */
}
#endif /* FREECIV_DEBUG */

#ifdef MENUS_GTK3
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
#endif /* MENUS_GTK3 */

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

  if (!menus_built) {
    return;
  }

  punits = get_units_in_focus();

  submenu = g_menu_new();

  i = 0;
  j = 0;
  /* Add the new action entries grouped by target kind. */
  for (tgt_kind_group = 0; tgt_kind_group < ATK_COUNT; tgt_kind_group++) {
    action_iterate(act_id) {
      struct action *paction = action_by_number(act_id);
      GSimpleAction *act;
      char actname[256];
      GMenuItem *item;
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
  GMenuItem *sub_item;                                                     \
  fc_snprintf(actname, sizeof(actname), "subtgt_%d", j);                   \
  act = g_simple_action_new(actname, NULL);                                \
  g_action_map_add_action(map, G_ACTION(act));                             \
  g_object_set_data(G_OBJECT(act), _sub_target_key_, _sub_target_);        \
  g_signal_connect(act, "activate", G_CALLBACK(unit_goto_and_callback),    \
                   paction);                                               \
  fc_snprintf(subname, sizeof(subname), "%s", _sub_target_name_);          \
  fc_snprintf(actname, sizeof(actname), "app.subtgt_%d", j++);             \
  sub_item = g_menu_item_new(subname, actname);                            \
  g_menu_append_item(sub_target_menu, sub_item);                           \
}

        switch (action_get_sub_target_kind(paction)) {
        case ASTK_BUILDING:
          improvement_iterate(pimpr) {
            CREATE_SUB_ITEM(pimpr, "end_building",
                            improvement_name_translation(pimpr));
          } improvement_iterate_end;
          break;
        case ASTK_TECH:
          advance_iterate(A_FIRST, ptech) {
            CREATE_SUB_ITEM(ptech, "end_tech",
                            advance_name_translation(ptech));
          } advance_iterate_end;
          break;
        case ASTK_EXTRA:
        case ASTK_EXTRA_NOT_THERE:
          extra_type_iterate(pextra) {
            if (!(action_creates_extra(paction, pextra)
                  || action_removes_extra(paction, pextra))) {
              /* Not relevant */
              continue;
            }
            CREATE_SUB_ITEM(pextra, "end_extra",
                            extra_name_translation(pextra));
          } extra_type_iterate_end;
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

        g_menu_append_submenu(submenu, name, G_MENU_MODEL(sub_target_menu));
      } else {
        item = g_menu_item_new(name, actname);
        g_signal_connect(act, "activate",
                         G_CALLBACK(unit_goto_and_callback), paction);
        g_menu_append_item(submenu, item);
      }
    } action_iterate_end;
  }
  g_menu_remove(unit_menu, 0);
  g_menu_insert_submenu(unit_menu, 0, _("Go to a_nd..."), G_MENU_MODEL(submenu));

  submenu = g_menu_new();
  menu_entry_init(submenu, "START_REVOLUTION");

  i = 0;
  governments_iterate(g) {
    if (g != game.government_during_revolution) {
      GMenuItem *item;
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
      item = g_menu_item_new(name, actname);
      g_menu_append_item(submenu, item);
    }
  } governments_iterate_end;
  g_menu_remove(gov_menu, 6);
  g_menu_insert_submenu(gov_menu, 6, _("_Government"), G_MENU_MODEL(submenu));

  submenu = g_menu_new();

  extra_type_by_cause_iterate(EC_ROAD, pextra) {
    if (pextra->buildable) {
      GMenuItem *item;
      char actname[256];
      GSimpleAction *act;

      fc_snprintf(actname, sizeof(actname), "path_%d", i);
      act = g_simple_action_new(actname, NULL);
      g_simple_action_set_enabled(act,
                                  can_units_do_activity_targeted(punits,
                                                                 ACTIVITY_GEN_ROAD,
                                                                 pextra));
      g_action_map_add_action(map, G_ACTION(act));
      g_signal_connect(act, "activate", G_CALLBACK(road_callback), pextra);

      fc_snprintf(actname, sizeof(actname), "app.path_%d", i++);
      item = g_menu_item_new(extra_name_translation(pextra), actname);
      g_menu_append_item(submenu, item);
    }
  } extra_type_by_cause_iterate_end;

  g_menu_remove(work_menu, 2);
  g_menu_insert_submenu(work_menu, 2, _("Build _Path"), G_MENU_MODEL(submenu));

  submenu = g_menu_new();

  extra_type_by_cause_iterate(EC_IRRIGATION, pextra) {
    if (pextra->buildable) {
      GMenuItem *item;
      char actname[256];
      GSimpleAction *act;

      fc_snprintf(actname, sizeof(actname), "irrig_%d", i);
      act = g_simple_action_new(actname, NULL);
      g_simple_action_set_enabled(act,
                                  can_units_do_activity_targeted(punits,
                                                                 ACTIVITY_IRRIGATE,
                                                                 pextra));
      g_action_map_add_action(map, G_ACTION(act));
      g_signal_connect(act, "activate", G_CALLBACK(irrigation_callback), pextra);

      fc_snprintf(actname, sizeof(actname), "app.irrig_%d", i++);
      item = g_menu_item_new(extra_name_translation(pextra), actname);
      g_menu_append_item(submenu, item);
    }
  } extra_type_by_cause_iterate_end;

  g_menu_remove(work_menu, 3);
  g_menu_insert_submenu(work_menu, 3, _("Build _Irrigation"), G_MENU_MODEL(submenu));

  submenu = g_menu_new();

  extra_type_by_cause_iterate(EC_MINE, pextra) {
    if (pextra->buildable) {
      GMenuItem *item;
      char actname[256];
      GSimpleAction *act;

      fc_snprintf(actname, sizeof(actname), "mine_%d", i);
      act = g_simple_action_new(actname, NULL);
      g_simple_action_set_enabled(act,
                                  can_units_do_activity_targeted(punits,
                                                                 ACTIVITY_MINE,
                                                                 pextra));
      g_action_map_add_action(map, G_ACTION(act));
      g_signal_connect(act, "activate", G_CALLBACK(mine_callback), pextra);

      fc_snprintf(actname, sizeof(actname), "app.mine_%d", i++);
      item = g_menu_item_new(extra_name_translation(pextra), actname);
      g_menu_append_item(submenu, item);
    }
  } extra_type_by_cause_iterate_end;

  g_menu_remove(work_menu, 4);
  g_menu_insert_submenu(work_menu, 4, _("Build _Mine"), G_MENU_MODEL(submenu));

  submenu = g_menu_new();

  extra_type_by_rmcause_iterate(ERM_CLEANPOLLUTION, pextra) {
    GMenuItem *item;
    char actname[256];
    GSimpleAction *act;

    fc_snprintf(actname, sizeof(actname), "clean_%d", i);
    act = g_simple_action_new(actname, NULL);
    g_simple_action_set_enabled(act,
                                can_units_do_activity_targeted(punits,
                                                               ACTIVITY_POLLUTION,
                                                               pextra));
    g_action_map_add_action(map, G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(clean_callback), pextra);

    fc_snprintf(actname, sizeof(actname), "app.clean_%d", i++);
    item = g_menu_item_new(extra_name_translation(pextra), actname);
    g_menu_append_item(submenu, item);
  } extra_type_by_rmcause_iterate_end;

  extra_type_by_rmcause_iterate(ERM_CLEANFALLOUT, pextra) {
    GMenuItem *item;
    char actname[256];
    GSimpleAction *act;

    fc_snprintf(actname, sizeof(actname), "clean_%d", i);
    act = g_simple_action_new(actname, NULL);
    g_simple_action_set_enabled(act,
                                can_units_do_activity_targeted(punits,
                                                               ACTIVITY_FALLOUT,
                                                               pextra));
    g_action_map_add_action(map, G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(clean_callback), pextra);

    fc_snprintf(actname, sizeof(actname), "app.clean_%d", i++);
    item = g_menu_item_new(extra_name_translation(pextra), actname);
    g_menu_append_item(submenu, item);
  } extra_type_by_rmcause_iterate_end;

  g_menu_remove(work_menu, 5);
  g_menu_insert_submenu(work_menu, 5, _("_Clean"), G_MENU_MODEL(submenu));

  submenu = g_menu_new();

  extra_type_by_cause_iterate(EC_BASE, pextra) {
    if (pextra->buildable) {
      GMenuItem *item;
      char actname[256];
      GSimpleAction *act;

      fc_snprintf(actname, sizeof(actname), "base_%d", i);
      act = g_simple_action_new(actname, NULL);
      g_simple_action_set_enabled(act,
                                  can_units_do_activity_targeted(punits,
                                                                 ACTIVITY_BASE,
                                                                 pextra));
      g_action_map_add_action(map, G_ACTION(act));
      g_signal_connect(act, "activate", G_CALLBACK(base_callback), pextra);

      fc_snprintf(actname, sizeof(actname), "app.base_%d", i++);
      item = g_menu_item_new(extra_name_translation(pextra), actname);
      g_menu_append_item(submenu, item);
    }
  } extra_type_by_cause_iterate_end;

  g_menu_remove(combat_menu, 1);
  g_menu_insert_submenu(combat_menu, 1, _("Build _Base"), G_MENU_MODEL(submenu));

#ifdef MENUS_GTK3
  bool units_all_same_tile = TRUE, units_all_same_type = TRUE;
  char acttext[128], irrtext[128], mintext[128], transtext[128];
  char cultext[128], plantext[128];
  struct terrain *pterrain;
  bool conn_possible;
  struct road_type *proad;
  struct extra_type_list *extras;
#endif /* MENUS_GTK3 */

  if (!menus_built || !can_client_change_view()) {
    return;
  }

  num_units = get_num_units_in_focus();

#ifdef MENUS_GTK3
  if (num_units > 0) {
    const struct tile *ptile = NULL;
    const struct unit_type *ptype = NULL;

    unit_list_iterate(punits, punit) {
      fc_assert((ptile==NULL) == (ptype==NULL));
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
#endif /* MENUS_GTK3 */

  menu_entry_group_set_sensitive(map, MGROUP_EDIT, editor_is_active());
  menu_entry_group_set_sensitive(map, MGROUP_PLAYING,
                                 can_client_issue_orders() && !editor_is_active());
  menu_entry_group_set_sensitive(map, MGROUP_UNIT,
                                 num_units > 0
                                 && can_client_issue_orders()
                                 && !editor_is_active());

  menu_entry_set_sensitive(map, "INFRA_DLG", terrain_control.infrapoints);

#ifdef MENUS_GTK3

  menu_entry_set_active("EDIT_MODE", game.info.is_edit_mode);
  menu_entry_set_sensitive("EDIT_MODE",
                           can_conn_enable_editing(&client.conn));
  editgui_refresh();
  menu_entry_set_sensitive("RALLY_DLG", can_client_issue_orders());

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
  }
#endif /* MENUS_GTK3 */

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
                           (can_units_do(punits, unit_can_add_or_build_city)
                            || can_units_do(punits, unit_can_help_build_wonder_here)));

#ifdef MENUS_GTK3
  /* Set base sensitivity. */
  if ((menu = find_menu("<MENU>/BUILD_BASE"))) {
    GtkWidget *iter;
    struct extra_type *pextra;

    for (iter = gtk_widget_get_first_child(menu);
         iter != NULL;
         iter = gtk_widget_get_next_sibling(iter)) {
      pextra = g_object_get_data(G_OBJECT(iter), "base");
      if (NULL != pextra) {
        gtk_widget_set_sensitive(GTK_WIDGET(iter),
                                 can_units_do_activity_targeted(punits,
                                                                ACTIVITY_BASE,
                                                                pextra));
      }
    }
  }

  /* Set road sensitivity. */
  if ((menu = find_menu("<MENU>/BUILD_PATH"))) {
    GtkWidget *iter;
    struct extra_type *pextra;

    for (iter = gtk_widget_get_first_child(menu);
         iter != NULL;
         iter = gtk_widget_get_next_sibling(iter)) {
      pextra = g_object_get_data(G_OBJECT(iter), "road");
      if (NULL != pextra) {
        gtk_widget_set_sensitive(GTK_WIDGET(iter),
                                 can_units_do_activity_targeted(punits,
                                                                ACTIVITY_GEN_ROAD,
                                                                pextra));
      }
    }
  }

  /* Set Go to and... action visibility. */
  if ((menu = find_menu("<MENU>/GOTO_AND"))) {
    GtkWidget *iter;
    struct action *paction;

    bool can_do_something = FALSE;

    /* Enable a menu item if it is theoretically possible that one of the
     * selected units can perform it. Checking if the action can be performed
     * at the current tile is pointless since it should be performed at the
     * target tile. */
    for (iter = gtk_widget_get_first_child(menu);
         iter != NULL;
         iter = gtk_widget_get_next_sibling(iter)) {
      paction = g_object_get_data(G_OBJECT(iter), "end_action");
      if (NULL != paction) {
        if (units_can_do_action(punits, paction->id, TRUE)) {
          gtk_widget_set_visible(GTK_WIDGET(iter), TRUE);
          gtk_widget_set_sensitive(GTK_WIDGET(iter), TRUE);
          can_do_something = TRUE;
        } else {
          gtk_widget_set_visible(GTK_WIDGET(iter), FALSE);
          gtk_widget_set_sensitive(GTK_WIDGET(iter), FALSE);
        }
      }
    }

    /* Only sensitive if an action may be possible. */
    menu_entry_set_sensitive("MENU_GOTO_AND", can_do_something);
  }

  menu_entry_set_sensitive("BUILD_ROAD",
                           (can_units_do_any_road(punits)
                            || can_units_do(punits,
                                            unit_can_est_trade_route_here)));
  menu_entry_set_sensitive("BUILD_IRRIGATION",
                           can_units_do_activity(punits, ACTIVITY_IRRIGATE));
#endif /* MENUS_GTK3 */

  menu_entry_set_sensitive(map, "CULTIVATE",
                           can_units_do_activity(punits, ACTIVITY_CULTIVATE));
  menu_entry_set_sensitive(map, "PLANT",
                           can_units_do_activity(punits, ACTIVITY_PLANT));

#ifdef MENUS_GTK3
  menu_entry_set_sensitive("BUILD_MINE",
                           can_units_do_activity(punits, ACTIVITY_MINE));
  menu_entry_set_sensitive("TRANSFORM_TERRAIN",
                           can_units_do_activity(punits, ACTIVITY_TRANSFORM));
#endif /* MENUS_GTK3 */

  menu_entry_set_sensitive(map, "FORTIFY",
                           can_units_do_activity(punits,
                                                 ACTIVITY_FORTIFYING));
  menu_entry_set_sensitive(map, "PARADROP",
                           can_units_do(punits, can_unit_paradrop));
  menu_entry_set_sensitive(map, "PILLAGE",
                           can_units_do_activity(punits, ACTIVITY_PILLAGE));

#ifdef MENUS_GTK3
  menu_entry_set_sensitive("BUILD_FORTRESS",
                           can_units_do_base_gui(punits, BASE_GUI_FORTRESS));
  menu_entry_set_sensitive("BUILD_AIRBASE",
                           can_units_do_base_gui(punits, BASE_GUI_AIRBASE));
  menu_entry_set_sensitive("CLEAN_POLLUTION",
                           (can_units_do_activity(punits, ACTIVITY_POLLUTION)
                            || can_units_do(punits, can_unit_paradrop)));
  menu_entry_set_sensitive("CLEAN_FALLOUT",
                           can_units_do_activity(punits, ACTIVITY_FALLOUT));
#endif /* MENUS_GTK3 */

  menu_entry_set_sensitive(map, "UNIT_SENTRY",
                           can_units_do_activity(punits, ACTIVITY_SENTRY));
  menu_entry_set_sensitive(map, "UNIT_HOMECITY",
                           can_units_do(punits, can_unit_change_homecity));
  menu_entry_set_sensitive(map, "UNIT_UPGRADE", units_can_upgrade(punits));
  menu_entry_set_sensitive(map, "UNIT_CONVERT", units_can_convert(punits));
  menu_entry_set_sensitive(map, "UNIT_DISBAND",
                           units_can_do_action(punits, ACTION_DISBAND_UNIT,
                                               TRUE));

  menu_entry_set_sensitive(map, "AUTO_SETTLER",
                           can_units_do(punits, can_unit_do_autosettlers));
  menu_entry_set_sensitive(map, "UNIT_EXPLORE",
                           can_units_do_activity(punits, ACTIVITY_EXPLORE));

#ifdef MENUS_GTK3
  /* "UNIT_CONVERT" dealt with below */
  menu_entry_set_sensitive("UNIT_UNLOAD_TRANSPORTER",
                           units_are_occupied(punits));
  menu_entry_set_sensitive("UNIT_BOARD",
                           units_can_load(punits));
  menu_entry_set_sensitive("UNIT_DEBOARD",
                           units_can_unload(punits));
  menu_entry_set_sensitive("UNIT_UNSENTRY",
                           units_have_activity_on_tile(punits,
                                                       ACTIVITY_SENTRY));

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
  } else if (units_have_type_flag(punits, UTYF_SETTLERS, TRUE)) {
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
      /* TRANS: Build road of specific type (Road/Railroad) */
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

  if (units_can_convert(punits)) {
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

    if (units_have_type_flag(punits, UTYF_SETTLERS, TRUE)) {
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

    if (units_have_type_flag(punits, UTYF_SETTLERS, TRUE)) {
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

#endif /* MENUS_GTK3 */
}

#ifdef MENUS_GTK3
/************************************************************************//**
  Add an accelerator to an item in the "Go to and..." menu.
****************************************************************************/
static void menu_unit_goto_and_add_accel(GtkWidget *item, action_id act_id,
                                         const guint accel_key,
                                         const GdkModifierType accel_mods)
{
  const char *path = gtk_menu_item_get_accel_path(GTK_MENU_ITEM(item));

  if (path == NULL) {
    char buf[MAX_LEN_NAME + strlen("<MENU>/GOTO_AND/")];

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
  GtkWidget *iter;
  GtkWidget *sub_menu;

  for (iter = gtk_widget_get_first_child(menu);
       iter != NULL; ) {
    GtkWidget *cur;

    if ((sub_menu = gtk_menu_item_get_submenu(iter)) != NULL) {
      menu_remove_previous_entries(GTK_MENU(sub_menu));
      gtk_widget_destroy(sub_menu);
    }
    cur = iter;
    iter = gtk_widget_get_next_sibling(iter);
    gtk_widget_destroy(GTK_WIDGET(cur));
  }
}
#endif /* MENUS_GTK3 */

/************************************************************************//**
  Initialize menus (sensitivity, name, etc.) based on the
  current state and current ruleset, etc.  Call menus_update().
****************************************************************************/
void real_menus_init(void)
{
  if (!menus_built) {
    return;
  }

#ifdef MENUS_GTK3
  GtkMenu *menu;

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
    GtkWidget *iter;
    GtkWidget *item;
    char buf[256];

    /* Remove previous government entries. */
    for (iter = gtk_widget_get_first_child(menu);
         iter != NULL; ) {
      GtkWidget *cur = iter;

      iter = gtk_widget_get_next_sibling(iter);
      if (g_object_get_data(G_OBJECT(cur), "government") != NULL
          || GTK_IS_SEPARATOR_MENU_ITEM(cur)) {
        gtk_widget_destroy(GTK_WIDGET(cur));
      }
    }

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
                        gui_options.draw_roads_rails);
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
  menu_entry_set_active("SHOW_FOCUS_UNIT",
                        gui_options.draw_focus_unit);
  menu_entry_set_active("SHOW_FOG_OF_WAR",
                        gui_options.draw_fog_of_war);

  view_menu_update_sensitivity();

  menu_entry_set_active("FULL_SCREEN", GUI_GTK_OPTION(fullscreen));

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
