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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include "astring.h"
#include "fcintl.h"
#include "log.h"

#include "game.h"
#include "government.h"
#include "map.h"
#include "mem.h"
#include "movement.h"
#include "support.h"
#include "unit.h"
#include "unitlist.h"

#include "chatline.h"
#include "cityrep.h"
#include "civclient.h"
#include "clinet.h"
#include "connectdlg_common.h"
#include "connectdlg.h"
#include "control.h"
#include "dialogs.h"
#include "editgui.h"
#include "finddlg.h"
#include "gotodlg.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "mapctrl.h"   /* center_on_unit */
#include "messagedlg.h"
#include "messagewin.h"
#include "optiondlg.h"
#include "options.h"
#include "packhand.h"
#include "pages.h"
#include "plrdlg.h"
#include "ratesdlg.h"
#include "repodlgs.h"
#include "spaceshipdlg.h"
#include "wldlg.h"

#include "menu.h"

static GtkItemFactory *item_factory = NULL;
static GtkWidget *main_menubar = NULL;
GtkAccelGroup *toplevel_accel = NULL;
static enum unit_activity road_activity;

static void menus_rename(const char *path, const char *s);
static void menus_set_active_no_callback(const gchar *path,
                                         gboolean active);

/****************************************************************
...
*****************************************************************/
enum MenuID {
  MENU_END_OF_LIST=0,

  MENU_GAME_OPTIONS,
  MENU_GAME_MSG_OPTIONS,
#ifdef DEBUG
  MENU_GAME_RELOAD_TILESET,
#endif
  MENU_GAME_SAVE_OPTIONS_ON_EXIT,
  MENU_GAME_SAVE_OPTIONS,
  MENU_GAME_SERVER_OPTIONS,
  MENU_GAME_SAVE_GAME,
  MENU_GAME_SAVE_QUICK, 
  MENU_GAME_OUTPUT_LOG,
  MENU_GAME_CLEAR_OUTPUT,
  MENU_GAME_LEAVE,
  MENU_GAME_QUIT,
  
  MENU_GOVERNMENT_TAX_RATE,
  MENU_GOVERNMENT_FIND_CITY,
  MENU_GOVERNMENT_WORKLISTS,
  MENU_GOVERNMENT_REVOLUTION,

  MENU_VIEW_SHOW_CITY_OUTLINES,
  MENU_VIEW_SHOW_MAP_GRID,
  MENU_VIEW_SHOW_NATIONAL_BORDERS,
  MENU_VIEW_SHOW_CITY_NAMES,
  MENU_VIEW_SHOW_CITY_GROWTH_TURNS,
  MENU_VIEW_SHOW_CITY_PRODUCTIONS,
  MENU_VIEW_SHOW_TERRAIN,
  MENU_VIEW_SHOW_COASTLINE,
  MENU_VIEW_SHOW_ROADS_RAILS,
  MENU_VIEW_SHOW_IRRIGATION,
  MENU_VIEW_SHOW_MINES,
  MENU_VIEW_SHOW_FORTRESS_AIRBASE,
  MENU_VIEW_SHOW_SPECIALS,
  MENU_VIEW_SHOW_POLLUTION,
  MENU_VIEW_SHOW_CITIES,
  MENU_VIEW_SHOW_UNITS,
  MENU_VIEW_SHOW_FOCUS_UNIT,
  MENU_VIEW_SHOW_FOG_OF_WAR,
  MENU_VIEW_FULL_SCREEN,
  MENU_VIEW_CENTER_VIEW,

  MENU_ORDER_BUILD_CITY,     /* shared with BUILD_WONDER */
  MENU_ORDER_ROAD,           /* shared with TRADEROUTE */
  MENU_ORDER_IRRIGATE,
  MENU_ORDER_MINE,
  MENU_ORDER_TRANSFORM,
  MENU_ORDER_FORTRESS,       /* shared with FORTIFY */
  MENU_ORDER_AIRBASE,
  MENU_ORDER_POLLUTION,      /* shared with PARADROP */
  MENU_ORDER_FALLOUT,
  MENU_ORDER_SENTRY,
  MENU_ORDER_PILLAGE,
  MENU_ORDER_HOMECITY,
  MENU_ORDER_UNLOAD_TRANSPORTER,
  MENU_ORDER_LOAD,
  MENU_ORDER_UNLOAD,
  MENU_ORDER_WAKEUP_OTHERS,
  MENU_ORDER_AUTO_SETTLER,   /* shared with AUTO_ATTACK */
  MENU_ORDER_AUTO_EXPLORE,
  MENU_ORDER_CONNECT_ROAD,
  MENU_ORDER_CONNECT_RAIL,
  MENU_ORDER_CONNECT_IRRIGATE,
  MENU_ORDER_GO_BUILD_CITY,
  MENU_ORDER_PATROL,
  MENU_ORDER_GOTO,
  MENU_ORDER_GOTO_CITY,
  MENU_ORDER_RETURN,
  MENU_ORDER_DISBAND,
  MENU_ORDER_UPGRADE,
  MENU_ORDER_DIPLOMAT_DLG,
  MENU_ORDER_NUKE,
  MENU_ORDER_SELECT_SAME_TYPE,
  MENU_ORDER_SELECT_SAME_TYPE_TILE,
  MENU_ORDER_WAIT,
  MENU_ORDER_DONE,

  MENU_REPORT_CITIES,
  MENU_REPORT_UNITS,
  MENU_REPORT_PLAYERS,
  MENU_REPORT_MAP_VIEW,
  MENU_REPORT_ECONOMY,
  MENU_REPORT_SCIENCE,
  MENU_REPORT_WOW,
  MENU_REPORT_TOP_CITIES,
  MENU_REPORT_MESSAGES,
  MENU_REPORT_DEMOGRAPHIC,
  MENU_REPORT_SPACESHIP,
 
  MENU_EDITOR_TOGGLE,
  MENU_EDITOR_RECALCULATE_BORDERS,
  MENU_EDITOR_TOGGLE_FOGOFWAR,

  MENU_HELP_LANGUAGES,
  MENU_HELP_CONNECTING,
  MENU_HELP_CONTROLS,
  MENU_HELP_CHATLINE,
  MENU_HELP_WORKLIST_EDITOR,
  MENU_HELP_CMA,
  MENU_HELP_PLAYING,
  MENU_HELP_IMPROVEMENTS,
  MENU_HELP_UNITS,
  MENU_HELP_COMBAT,
  MENU_HELP_ZOC,
  MENU_HELP_TECH,
  MENU_HELP_TERRAIN,
  MENU_HELP_WONDERS,
  MENU_HELP_GOVERNMENT,
  MENU_HELP_HAPPINESS,
  MENU_HELP_SPACE_RACE,
  MENU_HELP_COPYING,
  MENU_HELP_ABOUT
};


/****************************************************************
  This is the response callback for the dialog with the message:
  Leaving a local game will end it!
****************************************************************/
static void leave_local_game_response(GtkWidget* dialog, gint response)
{
  gtk_widget_destroy(dialog);
  if (response == GTK_RESPONSE_OK) {
    /* It might be killed already */
    if (client.conn.used) {
      /* It will also kill the server */
      disconnect_from_server();
    }
  }
}

/****************************************************************
  ...
 *****************************************************************/
static void game_menu_callback(gpointer callback_data,
    guint callback_action, GtkWidget *widget)
{
  switch(callback_action) {
  case MENU_GAME_OPTIONS:
    popup_option_dialog();
    break;
  case MENU_GAME_MSG_OPTIONS:
    popup_messageopt_dialog();
    break;
#ifdef DEBUG
  case MENU_GAME_RELOAD_TILESET:
    tilespec_reread(NULL);
    break;
#endif
  case MENU_GAME_SAVE_OPTIONS_ON_EXIT:
    if (save_options_on_exit ^ GTK_CHECK_MENU_ITEM(widget)->active) {
      save_options_on_exit ^= 1;
    }
    break;
  case MENU_GAME_SAVE_OPTIONS:
    save_options();
    break;
  case MENU_GAME_SERVER_OPTIONS:
    popup_settable_options_dialog();
    break;
  case MENU_GAME_SAVE_GAME:
    popup_save_dialog();
    break;
  case MENU_GAME_SAVE_QUICK:
    send_save_game(NULL);
    break;
  case MENU_GAME_OUTPUT_LOG:
    log_output_window();
    break;
  case MENU_GAME_CLEAR_OUTPUT:
    clear_output_window();
    break;
  case MENU_GAME_LEAVE:
    if (is_server_running()) {
      GtkWidget* dialog = gtk_message_dialog_new(NULL,
	  0,
	  GTK_MESSAGE_WARNING,
	  GTK_BUTTONS_OK_CANCEL,
	  _("Leaving a local game will end it!"));
      setup_dialog(dialog, toplevel);
      gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);
      g_signal_connect(dialog, "response", 
	  G_CALLBACK(leave_local_game_response), NULL);
      gtk_window_present(GTK_WINDOW(dialog));
    } else {
      disconnect_from_server();
    }
    break;
  case MENU_GAME_QUIT:
    popup_quit_dialog();
    break;
  }
}


/****************************************************************
...
*****************************************************************/
static void government_menu_callback(gpointer callback_data,
				  guint callback_action, GtkWidget *widget)
{
  switch(callback_action) {
  case MENU_GOVERNMENT_TAX_RATE:
    popup_rates_dialog();
    break;
  case MENU_GOVERNMENT_FIND_CITY:
    popup_find_dialog();
    break;
  case MENU_GOVERNMENT_WORKLISTS:
    popup_worklists_report();
    break;
  case MENU_GOVERNMENT_REVOLUTION:
    popup_revolution_dialog(NULL);
    break;
  }
}


static void menus_set_sensitive(const char *, int);
/****************************************************************
...
*****************************************************************/
static void view_menu_callback(gpointer callback_data, guint callback_action,
			       GtkWidget *widget)
{
  switch(callback_action) {
  case MENU_VIEW_SHOW_CITY_OUTLINES:
    if (draw_city_outlines ^ GTK_CHECK_MENU_ITEM(widget)->active) {
      key_city_outlines_toggle();
    }
    break;
  case MENU_VIEW_SHOW_MAP_GRID:
    if (draw_map_grid ^ GTK_CHECK_MENU_ITEM(widget)->active)
      key_map_grid_toggle();
    break;
  case MENU_VIEW_SHOW_NATIONAL_BORDERS:
    if (draw_borders ^ GTK_CHECK_MENU_ITEM(widget)->active) {
      key_map_borders_toggle();
    }
    break;
  case MENU_VIEW_SHOW_CITY_NAMES:
    if (draw_city_names ^ GTK_CHECK_MENU_ITEM(widget)->active) {
      key_city_names_toggle();

      if (!draw_full_citybar) {
        /* The "full" city bar (i.e. the new way of drawing the
         * city name), can draw the city growth even without drawing
         * the city name. But the old method cannot. */
        menus_set_sensitive("<main>/_View/City G_rowth", draw_city_names);
      }
    }
    break;
  case MENU_VIEW_SHOW_CITY_GROWTH_TURNS:
    if (draw_city_growth ^ GTK_CHECK_MENU_ITEM(widget)->active)
      key_city_growth_toggle();
    break;
  case MENU_VIEW_SHOW_CITY_PRODUCTIONS:
    if (draw_city_productions ^ GTK_CHECK_MENU_ITEM(widget)->active)
      key_city_productions_toggle();
    break;
  case MENU_VIEW_SHOW_TERRAIN:
    if (draw_terrain ^ GTK_CHECK_MENU_ITEM(widget)->active) {
      key_terrain_toggle();
      menus_set_sensitive("<main>/View/Coastline", !draw_terrain);
    }
    break;
  case MENU_VIEW_SHOW_COASTLINE:
    if (draw_coastline ^ GTK_CHECK_MENU_ITEM(widget)->active)
      key_coastline_toggle();
    break;
  case MENU_VIEW_SHOW_ROADS_RAILS:
    if (draw_roads_rails ^ GTK_CHECK_MENU_ITEM(widget)->active)
      key_roads_rails_toggle();
    break;
  case MENU_VIEW_SHOW_IRRIGATION:
    if (draw_irrigation ^ GTK_CHECK_MENU_ITEM(widget)->active)
      key_irrigation_toggle();
    break;
  case MENU_VIEW_SHOW_MINES:
    if (draw_mines ^ GTK_CHECK_MENU_ITEM(widget)->active)
      key_mines_toggle();
    break;
  case MENU_VIEW_SHOW_FORTRESS_AIRBASE:
    if (draw_fortress_airbase ^ GTK_CHECK_MENU_ITEM(widget)->active)
      key_fortress_airbase_toggle();
    break;
  case MENU_VIEW_SHOW_SPECIALS:
    if (draw_specials ^ GTK_CHECK_MENU_ITEM(widget)->active)
      key_specials_toggle();
    break;
  case MENU_VIEW_SHOW_POLLUTION:
    if (draw_pollution ^ GTK_CHECK_MENU_ITEM(widget)->active)
      key_pollution_toggle();
    break;
  case MENU_VIEW_SHOW_CITIES:
    if (draw_cities ^ GTK_CHECK_MENU_ITEM(widget)->active)
      key_cities_toggle();
    break;
  case MENU_VIEW_SHOW_UNITS:
    if (draw_units ^ GTK_CHECK_MENU_ITEM(widget)->active) {
      key_units_toggle();
      menus_set_sensitive("<main>/View/Focus Unit", !draw_units);
    }
    break;
  case MENU_VIEW_SHOW_FOCUS_UNIT:
    if (draw_focus_unit ^ GTK_CHECK_MENU_ITEM(widget)->active)
      key_focus_unit_toggle();
    break;
  case MENU_VIEW_SHOW_FOG_OF_WAR:
    if (draw_fog_of_war ^ GTK_CHECK_MENU_ITEM(widget)->active)
      key_fog_of_war_toggle();
    break;
  case MENU_VIEW_FULL_SCREEN:
    if (fullscreen_mode ^ GTK_CHECK_MENU_ITEM(widget)->active) {
      fullscreen_mode ^= 1;

      if (fullscreen_mode) {
	gtk_window_fullscreen(GTK_WINDOW(toplevel));
      } else {
	gtk_window_unfullscreen(GTK_WINDOW(toplevel));
      }
    }
    break;
  case MENU_VIEW_CENTER_VIEW:
    center_on_unit();
    break;
  }
}


/****************************************************************
...
*****************************************************************/
static void orders_menu_callback(gpointer callback_data,
				 guint callback_action, GtkWidget *widget)
{
  switch(callback_action) {
  case MENU_ORDER_SELECT_SAME_TYPE:
    request_unit_select_same_type(get_units_in_focus());
    break;
  case MENU_ORDER_SELECT_SAME_TYPE_TILE:
    request_unit_select_same_type_tile(get_units_in_focus());
    break;
  case MENU_ORDER_BUILD_CITY:
    unit_list_iterate(get_units_in_focus(), punit) {
      /* FIXME: this can provide different actions for different units...
       * not good! */
      /* Enable the button for adding to a city in all cases, so we
	 get an eventual error message from the server if we try. */
      if (can_unit_add_or_build_city(punit)) {
	request_unit_build_city(punit);
      } else {
	request_unit_caravan_action(punit, PACKET_UNIT_HELP_BUILD_WONDER);
      }
    } unit_list_iterate_end;
    break;
  case MENU_ORDER_ROAD:
    unit_list_iterate(get_units_in_focus(), punit) {
      /* FIXME: this can provide different actions for different units...
       * not good! */
      if (unit_can_est_traderoute_here(punit))
	key_unit_traderoute();
      else
	key_unit_road();
    } unit_list_iterate_end;
    break;
   case MENU_ORDER_IRRIGATE:
    key_unit_irrigate();
    break;
   case MENU_ORDER_MINE:
    key_unit_mine();
    break;
   case MENU_ORDER_TRANSFORM:
    key_unit_transform();
    break;
  case MENU_ORDER_FORTRESS:
    unit_list_iterate(get_units_in_focus(), punit) {
      /* FIXME: this can provide different actions for different units...
       * not good! */
      struct base_type *pbase = get_base_by_gui_type(BASE_GUI_FORTRESS,
                                                     punit, punit->tile);
      if (pbase) {
	key_unit_fortress();
      } else {
	key_unit_fortify();
      }
    } unit_list_iterate_end;
    break;
   case MENU_ORDER_AIRBASE:
    key_unit_airbase(); 
    break;
   case MENU_ORDER_POLLUTION:
    unit_list_iterate(get_units_in_focus(), punit) {
      /* FIXME: this can provide different actions for different units...
       * not good! */
      if (can_unit_paradrop(punit))
	key_unit_paradrop();
      else
	key_unit_pollution();
    } unit_list_iterate_end;
    break;
   case MENU_ORDER_FALLOUT:
    key_unit_fallout();
    break;
   case MENU_ORDER_SENTRY:
    key_unit_sentry();
    break;
   case MENU_ORDER_PILLAGE:
    key_unit_pillage();
    break;
   case MENU_ORDER_HOMECITY:
    key_unit_homecity();
    break;
   case MENU_ORDER_UNLOAD_TRANSPORTER:
    key_unit_unload_all();
    break;
  case MENU_ORDER_LOAD:
    unit_list_iterate(get_units_in_focus(), punit) {
      request_unit_load(punit, NULL);
    } unit_list_iterate_end;
    break;
  case MENU_ORDER_UNLOAD:
    unit_list_iterate(get_units_in_focus(), punit) {
      request_unit_unload(punit);
    } unit_list_iterate_end;
    break;
   case MENU_ORDER_WAKEUP_OTHERS:
    key_unit_wakeup_others();
    break;
  case MENU_ORDER_AUTO_SETTLER:
    unit_list_iterate(get_units_in_focus(), punit) {
      request_unit_autosettlers(punit);
    } unit_list_iterate_end;
    break;
   case MENU_ORDER_AUTO_EXPLORE:
    key_unit_auto_explore();
    break;
   case MENU_ORDER_CONNECT_ROAD:
    key_unit_connect(ACTIVITY_ROAD);
    break;
  case MENU_ORDER_GO_BUILD_CITY:
    request_unit_goto(ORDER_BUILD_CITY);
    break;
   case MENU_ORDER_CONNECT_RAIL:
    key_unit_connect(ACTIVITY_RAILROAD);
    break;
   case MENU_ORDER_CONNECT_IRRIGATE:
    key_unit_connect(ACTIVITY_IRRIGATE);
    break;
   case MENU_ORDER_PATROL:
    key_unit_patrol();
    break;
   case MENU_ORDER_GOTO:
    key_unit_goto();
    break;
  case MENU_ORDER_GOTO_CITY:
    if (get_num_units_in_focus() > 0) {
      popup_goto_dialog();
    }
    break;
  case MENU_ORDER_RETURN:
    unit_list_iterate(get_units_in_focus(), punit) {
      request_unit_return(punit);
    } unit_list_iterate_end;
    break;
   case MENU_ORDER_DISBAND:
    key_unit_disband();
    break;
  case MENU_ORDER_UPGRADE:
    popup_upgrade_dialog(get_units_in_focus());
    break;
   case MENU_ORDER_DIPLOMAT_DLG:
    key_unit_diplomat_actions();
    break;
   case MENU_ORDER_NUKE:
    key_unit_nuke();
    break;
   case MENU_ORDER_WAIT:
    key_unit_wait();
    break;
   case MENU_ORDER_DONE:
    key_unit_done();
    break;
  }
}


/****************************************************************
...
*****************************************************************/
static void reports_menu_callback(gpointer callback_data,
				  guint callback_action, GtkWidget *widget)
{
  switch(callback_action) {
   case MENU_REPORT_CITIES:
    popup_city_report_dialog(TRUE);
    break;
   case MENU_REPORT_UNITS:
    popup_activeunits_report_dialog(TRUE);
    break;
  case MENU_REPORT_PLAYERS:
    popup_players_dialog(TRUE);
    break;
  case MENU_REPORT_MAP_VIEW:
    map_canvas_focus();
    break;
   case MENU_REPORT_ECONOMY:
    popup_economy_report_dialog(TRUE);
    break;
   case MENU_REPORT_SCIENCE:
    popup_science_dialog(TRUE);
    break;
   case MENU_REPORT_WOW:
    send_report_request(REPORT_WONDERS_OF_THE_WORLD);
    break;
   case MENU_REPORT_TOP_CITIES:
    send_report_request(REPORT_TOP_5_CITIES);
    break;
  case MENU_REPORT_MESSAGES:
    popup_meswin_dialog(TRUE);
    break;
   case MENU_REPORT_DEMOGRAPHIC:
    send_report_request(REPORT_DEMOGRAPHIC);
    break;
  case MENU_REPORT_SPACESHIP:
    if (NULL != client.conn.playing) {
      popup_spaceship_dialog(client.conn.playing);
    }
    break;
  }
}

/****************************************************************************
  Callback function for when an item is chosen from the "editor" menu.
****************************************************************************/
static void editor_menu_callback(gpointer callback_data,
                                 guint callback_action, GtkWidget *widget)
{   
  switch(callback_action) {
  case MENU_EDITOR_TOGGLE:
    key_editor_toggle();
    popdown_science_dialog(); /* Unreachbale techs in reqtree on/off */

    /* Because the user click on this check menu item would
     * cause the checkmark to appear, indicating wrongly that
     * edit mode is activated, reset the checkmark to the
     * correct, expected setting. */
    menus_set_active_no_callback("<main>/_Edit/Editing _Mode",
                                 game.info.is_edit_mode);
    break;
  case MENU_EDITOR_RECALCULATE_BORDERS:
    key_editor_recalculate_borders();
    break;
  case MENU_EDITOR_TOGGLE_FOGOFWAR:
    key_editor_toggle_fogofwar();
    break;
  default:
    break;
  }
}

/****************************************************************
...
*****************************************************************/
static void help_menu_callback(gpointer callback_data,
			       guint callback_action, GtkWidget *widget)
{
  switch(callback_action) {
  case MENU_HELP_LANGUAGES:
    popup_help_dialog_string(HELP_LANGUAGES_ITEM);
    break;
  case MENU_HELP_CONNECTING:
    popup_help_dialog_string(HELP_CONNECTING_ITEM);
    break;
  case MENU_HELP_CONTROLS:
    popup_help_dialog_string(HELP_CONTROLS_ITEM);
    break;
  case MENU_HELP_CHATLINE:
    popup_help_dialog_string(HELP_CHATLINE_ITEM);
    break;
  case MENU_HELP_WORKLIST_EDITOR:
    popup_help_dialog_string(HELP_WORKLIST_EDITOR_ITEM);
    break;
  case MENU_HELP_CMA:
    popup_help_dialog_string(HELP_CMA_ITEM);
    break;
  case MENU_HELP_PLAYING:
    popup_help_dialog_string(HELP_PLAYING_ITEM);
    break;
  case MENU_HELP_IMPROVEMENTS:
    popup_help_dialog_string(HELP_IMPROVEMENTS_ITEM);
    break;
  case MENU_HELP_UNITS:
    popup_help_dialog_string(HELP_UNITS_ITEM);
    break;
  case MENU_HELP_COMBAT:
    popup_help_dialog_string(HELP_COMBAT_ITEM);
    break;
  case MENU_HELP_ZOC:
    popup_help_dialog_string(HELP_ZOC_ITEM);
    break;
  case MENU_HELP_TECH:
    popup_help_dialog_string(HELP_TECHS_ITEM);
    break;
  case MENU_HELP_TERRAIN:
    popup_help_dialog_string(HELP_TERRAIN_ITEM);
    break;
  case MENU_HELP_WONDERS:
    popup_help_dialog_string(HELP_WONDERS_ITEM);
    break;
  case MENU_HELP_GOVERNMENT:
    popup_help_dialog_string(HELP_GOVERNMENT_ITEM);
    break;
  case MENU_HELP_HAPPINESS:
    popup_help_dialog_string(HELP_HAPPINESS_ITEM);
    break;
  case MENU_HELP_SPACE_RACE:
    popup_help_dialog_string(HELP_SPACE_RACE_ITEM);
    break;
  case MENU_HELP_COPYING:
    popup_help_dialog_string(HELP_COPYING_ITEM);
    break;
  case MENU_HELP_ABOUT:
    popup_help_dialog_string(HELP_ABOUT_ITEM);
    break;
  }
}

/* This is the GtkItemFactoryEntry structure used to generate new menus.
          Item 1: The menu path. The letter after the underscore indicates an
                  accelerator key once the menu is open.
          Item 2: The accelerator key for the entry
          Item 3: The callback function.
          Item 4: The callback action.  This changes the parameters with
                  which the function is called.  The default is 0.
          Item 5: The item type, used to define what kind of an item it is.
                  Here are the possible values:

                  NULL               -> "<Item>"
                  ""                 -> "<Item>"
                  "<Title>"          -> create a title item
                  "<Item>"           -> create a simple item
                  "<CheckItem>"      -> create a check item
                  "<ToggleItem>"     -> create a toggle item
                  "<RadioItem>"      -> create a radio item
                  <path>             -> path of a radio item to link against
                  "<Separator>"      -> create a separator
                  "<Branch>"         -> create an item to hold sub items
                  "<LastBranch>"     -> create a right justified branch 

Important: The underscore is NOT just for show (see Item 1 above)!
           At the top level, use with "Alt" key to open the menu.
	   Some less often used commands in the Order menu are not underscored
	   due to possible conflicts.
*/
static GtkItemFactoryEntry menu_items[]	=
{
  /* Game menu ... */
  { "/" N_("_Game"),					NULL,
	NULL,			0,					"<Branch>"	},
  { "/" N_("Game") "/tearoff1",				NULL,
	NULL,			0,					"<Tearoff>"	},
  { "/" N_("Game") "/" N_("_Chat"),			NULL,
	NULL,			0,					"<Branch>"	},
  { "/" N_("Game") "/" N_("_Chat") "/" N_("_Clear Log"),		NULL,
	game_menu_callback,	MENU_GAME_CLEAR_OUTPUT					},
  { "/" N_("Game") "/" N_("_Chat") "/" N_("_Write Log"),		NULL,
	game_menu_callback,	MENU_GAME_OUTPUT_LOG					},
  { "/" N_("Game") "/" N_("_Government"),		NULL,
	NULL,			0,					"<Branch>"	},
  { "/" N_("Game") "/" N_("_Government") "/" N_("_Tax Rates"),		"<shift>t",
	government_menu_callback,	MENU_GOVERNMENT_TAX_RATE			},
  { "/" N_("Game") "/" N_("_Government") "/" N_("_Revolution..."),	"<shift>r",
	government_menu_callback,	MENU_GOVERNMENT_REVOLUTION			},
  { "/" N_("Game") "/" N_("_Government") "/sep1", NULL,
	NULL,			0,					"<Separator>"	},
  { "/" N_("Game") "/" N_("_Options"),			NULL,
	NULL,			0,					"<Branch>"	},
  { "/" N_("Game") "/" N_("_Options") "/" N_("_Local Client"),		NULL,
	game_menu_callback,	MENU_GAME_OPTIONS					},
  { "/" N_("Game") "/" N_("_Options") "/" N_("_Message"),	NULL,
	game_menu_callback,	MENU_GAME_MSG_OPTIONS					},
  { "/" N_("Game") "/" N_("_Options") "/" N_("_Remote Server"),	NULL,
	game_menu_callback,	MENU_GAME_SERVER_OPTIONS},
  { "/" N_("Game") "/" N_("_Options") "/" N_("Save Options _Now"),		NULL,
	game_menu_callback,	MENU_GAME_SAVE_OPTIONS					},
  { "/" N_("Game") "/" N_("_Options") "/" N_("Save Options on _Exit"),	NULL,
	game_menu_callback,	MENU_GAME_SAVE_OPTIONS_ON_EXIT,		"<CheckItem>"	},
  { "/" N_("Game") "/sep4",				NULL,
	NULL,			0,					"<Separator>"	},
#ifdef DEBUG
  { "/" N_("Game") "/" N_("_Reload Tileset"), "<ctrl><alt>r",
    game_menu_callback, MENU_GAME_RELOAD_TILESET },
#endif
  { "/" N_("Game") "/" N_("_Save Game"),		NULL,
	game_menu_callback,	MENU_GAME_SAVE_QUICK, 			"<StockItem>",
	GTK_STOCK_SAVE									},
  { "/" N_("Game") "/" N_("Save Game _As..."),		NULL,
	game_menu_callback,	MENU_GAME_SAVE_GAME,			"<StockItem>",
	GTK_STOCK_SAVE_AS								},
  { "/" N_("Game") "/sep6",				NULL,
	NULL,			0,					"<Separator>"	},
  { "/" N_("Game") "/" N_("_Leave"),			NULL,
	game_menu_callback,	MENU_GAME_LEAVE						},
  { "/" N_("Game") "/" N_("_Quit"),			NULL,
	game_menu_callback,	MENU_GAME_QUIT,				"<StockItem>",
	GTK_STOCK_QUIT									},

  /* was Government menu ... */
  { "/" N_("_Edit"),					NULL,
	NULL,			0,					"<Branch>"	},
  { "/" N_("_Edit") "/tearoff1",			NULL,
	NULL,			0,					"<Tearoff>"	},
  { "/" N_("_Edit") "/" N_("_Find City"),		"<shift>f",
	government_menu_callback,	MENU_GOVERNMENT_FIND_CITY			},
  { "/" N_("_Edit") "/" N_("_Worklists"),		"<control>l",
	government_menu_callback,	MENU_GOVERNMENT_WORKLISTS			},
  { "/" N_("_Edit") "/sep1",				NULL,
	NULL,			0,					"<Separator>"	},
  /* was Editor menu */
  { "/" N_("_Edit") "/" N_("Editing _Mode"), "<control>e",
	editor_menu_callback, MENU_EDITOR_TOGGLE, "<CheckItem>" },
  { "/" N_("_Edit") "/" N_("Recalculate _Borders"), NULL,
	editor_menu_callback, MENU_EDITOR_RECALCULATE_BORDERS },
  { "/" N_("_Edit") "/" N_("Toggle Fog-of-war"), "<control>f",
	editor_menu_callback, MENU_EDITOR_TOGGLE_FOGOFWAR },

  /* View menu ... */
  { "/" N_("_View"),					NULL,
	NULL,			0,					"<Branch>"	},
  { "/" N_("View") "/tearoff1",				NULL,
	NULL,			0,					"<Tearoff>"	},
  { "/" N_("View") "/" N_("City Outlines"), "<control>y",
    view_menu_callback, MENU_VIEW_SHOW_CITY_OUTLINES, "<CheckItem>"},
  { "/" N_("View") "/" N_("Map _Grid"),			"<control>g",
	view_menu_callback,	MENU_VIEW_SHOW_MAP_GRID,		"<CheckItem>"	},
  { "/" N_("View") "/" N_("National _Borders"),		"<control>b",
	view_menu_callback,	MENU_VIEW_SHOW_NATIONAL_BORDERS,	"<CheckItem>"	},
  { "/" N_("View") "/" N_("City _Names"),		"<control>n",
	view_menu_callback,	MENU_VIEW_SHOW_CITY_NAMES,		"<CheckItem>"	},
  { "/" N_("View") "/" N_("City G_rowth"),		"<control>r",
	view_menu_callback,	MENU_VIEW_SHOW_CITY_GROWTH_TURNS,
	"<CheckItem>"	},
  { "/" N_("View") "/" N_("City _Productions"),		"<control>p",
	view_menu_callback,	MENU_VIEW_SHOW_CITY_PRODUCTIONS,	"<CheckItem>"	},
  { "/" N_("View") "/sep1",				NULL,
	NULL,			0,					"<Separator>"	},
  { "/" N_("View") "/" N_("Terrain"),                   NULL,
        view_menu_callback,     MENU_VIEW_SHOW_TERRAIN,	                "<CheckItem>"   },
  { "/" N_("View") "/" N_("Coastline"),	                NULL,
        view_menu_callback,     MENU_VIEW_SHOW_COASTLINE,       	"<CheckItem>"   },
  { "/" N_("View") "/" N_("Improvements"),		NULL,
	NULL,			0,					"<Branch>"	},
  { "/" N_("View") "/" N_("Improvements") "/tearoff1",	NULL,
	NULL,			0,					"<Tearoff>"	},
  { "/" N_("View") "/" N_("Improvements") "/" N_("Roads & Rails"), NULL,
	view_menu_callback,	MENU_VIEW_SHOW_ROADS_RAILS,		"<CheckItem>"	},
  { "/" N_("View") "/" N_("Improvements") "/" N_("Irrigation"), NULL,
	view_menu_callback,	MENU_VIEW_SHOW_IRRIGATION,		"<CheckItem>"	},
  { "/" N_("View") "/" N_("Improvements") "/" N_("Mines"),	NULL,
	view_menu_callback,	MENU_VIEW_SHOW_MINES,			"<CheckItem>"	},
  { "/" N_("View") "/" N_("Improvements") "/" N_("Fortress & Airbase"), NULL,
	view_menu_callback,	MENU_VIEW_SHOW_FORTRESS_AIRBASE,	"<CheckItem>"	},
  { "/" N_("View") "/" N_("Specials"),			NULL,
	view_menu_callback,	MENU_VIEW_SHOW_SPECIALS,		"<CheckItem>"	},
  { "/" N_("View") "/" N_("Pollution & Fallout"),	NULL,
	view_menu_callback,	MENU_VIEW_SHOW_POLLUTION,		"<CheckItem>"	},
  { "/" N_("View") "/" N_("Cities"),			NULL,
	view_menu_callback,	MENU_VIEW_SHOW_CITIES,			"<CheckItem>"	},
  { "/" N_("View") "/" N_("Units"),			NULL,
	view_menu_callback,	MENU_VIEW_SHOW_UNITS,			"<CheckItem>"	},
  { "/" N_("View") "/" N_("Focus Unit"),		NULL,
	view_menu_callback,	MENU_VIEW_SHOW_FOCUS_UNIT,		"<CheckItem>"	},
  { "/" N_("View") "/" N_("Fog of War"),		NULL,
	view_menu_callback,	MENU_VIEW_SHOW_FOG_OF_WAR,		"<CheckItem>"	},
  { "/" N_("View") "/sep2",				NULL,
	NULL,			0,					"<Separator>"	},
  { "/" N_("View") "/" N_("_Full Screen"),		"<alt>Return",
	view_menu_callback,	MENU_VIEW_FULL_SCREEN,			"<CheckItem>"	},
  { "/" N_("View") "/sep3",				NULL,
	NULL,			0,					"<Separator>"	},
  { "/" N_("View") "/" N_("_Center View"),		"c",
	view_menu_callback,	MENU_VIEW_CENTER_VIEW					},
  /* Orders menu ... */
  { "/" N_("_Orders"),					NULL,
	NULL,			0,					"<Branch>"	},
  { "/" N_("Orders") "/tearoff1",			NULL,
	NULL,			0,					"<Tearoff>"	},
  { "/" N_("Orders") "/" N_("_Build City"),		"b",
	orders_menu_callback,	MENU_ORDER_BUILD_CITY					},
  { "/" N_("Orders") "/" N_("Build _Road"),		"r",
	orders_menu_callback,	MENU_ORDER_ROAD						},
  { "/" N_("Orders") "/" N_("Build _Irrigation"),	"i",
	orders_menu_callback,	MENU_ORDER_IRRIGATE					},
  { "/" N_("Orders") "/" N_("Build _Mine"),		"m",
	orders_menu_callback,	MENU_ORDER_MINE						},
  { "/" N_("Orders") "/" N_("Transf_orm Terrain"),	"o",
	orders_menu_callback,	MENU_ORDER_TRANSFORM					},
  { "/" N_("Orders") "/" N_("Build _Fortress"),		"f",
	orders_menu_callback,	MENU_ORDER_FORTRESS					},
  { "/" N_("Orders") "/" N_("Build Airbas_e"),		"e",
	orders_menu_callback,	MENU_ORDER_AIRBASE					},
  { "/" N_("Orders") "/" N_("Clean _Pollution"),	"p",
	orders_menu_callback,	MENU_ORDER_POLLUTION					},
  { "/" N_("Orders") "/" N_("Clean _Nuclear Fallout"),	"n",
	orders_menu_callback,	MENU_ORDER_FALLOUT					},
  { "/" N_("Orders") "/sep1",			NULL,
	NULL,			0,					"<Separator>"	},
  { "/" N_("Orders") "/" N_("_Sentry"),			"s",
	orders_menu_callback,	MENU_ORDER_SENTRY					},
  { "/" N_("Orders") "/" N_("Pillage"),		        "<shift>p",
	orders_menu_callback,	MENU_ORDER_PILLAGE					},
  { "/" N_("Orders") "/sep2",				NULL,
	NULL,			0,					"<Separator>"	},
  { "/" N_("Orders") "/" N_("Make _Homecity"),		"h",
	orders_menu_callback,	MENU_ORDER_HOMECITY					},
  { "/" N_("Orders") "/" N_("_Load"),			"l",
    orders_menu_callback, MENU_ORDER_LOAD},
  { "/" N_("Orders") "/" N_("_Unload Transporter"),	"<shift>u",
	orders_menu_callback,	MENU_ORDER_UNLOAD_TRANSPORTER					},
  { "/" N_("Orders") "/" N_("_Unload"),			"u",
    orders_menu_callback, MENU_ORDER_UNLOAD},
  { "/" N_("Orders") "/" N_("Wake up o_thers"),		"<shift>w",
	orders_menu_callback,	MENU_ORDER_WAKEUP_OTHERS				},
  { "/" N_("Orders") "/sep3",				NULL,
	NULL,			0,					"<Separator>"	},
  { "/" N_("Orders") "/" N_("_Auto Settler"),		"a",
	orders_menu_callback,	MENU_ORDER_AUTO_SETTLER					},
  { "/" N_("Orders") "/" N_("Auto E_xplore"),		"x",
	orders_menu_callback,	MENU_ORDER_AUTO_EXPLORE					},
  {"/" N_("Orders") "/" N_("_Connect") "/" N_("_Road"), "<ctrl><shift>r",
   orders_menu_callback, MENU_ORDER_CONNECT_ROAD},
  {"/" N_("Orders") "/" N_("_Connect") "/" N_("Rai_l"), "<ctrl><shift>l",
   orders_menu_callback, MENU_ORDER_CONNECT_RAIL},
  {"/" N_("Orders") "/" N_("_Connect") "/" N_("_Irrigate"), "<ctrl><shift>i",
   orders_menu_callback, MENU_ORDER_CONNECT_IRRIGATE},
  {"/" N_("Orders") "/" N_("Go _to") "/" N_("_Build city"), "<ctrl><shift>b",
   orders_menu_callback, MENU_ORDER_GO_BUILD_CITY},
  { "/" N_("Orders") "/" N_("Patrol (_Q)"),		"q",
	orders_menu_callback,	MENU_ORDER_PATROL					},
  { "/" N_("Orders") "/" N_("_Go to"),			"g",
	orders_menu_callback,	MENU_ORDER_GOTO						},
  { "/" N_("Orders") "/" N_("Go\\/Airlift to City"),	"<shift>l",
	orders_menu_callback,	MENU_ORDER_GOTO_CITY					},
  { "/" N_("Orders") "/" N_("Return to nearest city"),	"<shift>g",
	orders_menu_callback,	MENU_ORDER_RETURN },
  { "/" N_("Orders") "/sep4",				NULL,
	NULL,			0,					"<Separator>"	},
  { "/" N_("Orders") "/" N_("Disband Unit"),		"<shift>d",
	orders_menu_callback,	MENU_ORDER_DISBAND					},
  { "/" N_("Orders") "/" N_("Upgrade unit"), "<ctrl>u",
    orders_menu_callback, MENU_ORDER_UPGRADE },
  { "/" N_("Orders") "/" N_("Diplomat\\/Spy Actions"),	"d",
	orders_menu_callback,	MENU_ORDER_DIPLOMAT_DLG					},
  { "/" N_("Orders") "/" N_("Explode Nuclear"),        "<shift>n",
	orders_menu_callback,	MENU_ORDER_NUKE						},
  { "/" N_("Orders") "/sep5",				NULL,
	NULL,			0,					"<Separator>"	},
  { "/" N_("Orders") "/" N_("Select same type"), "y",
    orders_menu_callback, MENU_ORDER_SELECT_SAME_TYPE },
  { "/" N_("Orders") "/" N_("Select same type in tile"), "<shift>y",
    orders_menu_callback, MENU_ORDER_SELECT_SAME_TYPE_TILE },
  { "/" N_("Orders") "/" N_("_Wait"),			"w",
	orders_menu_callback,	MENU_ORDER_WAIT						},
  { "/" N_("Orders") "/" N_("Done"),			"space",
	orders_menu_callback,	MENU_ORDER_DONE						},
  /* Reports menu ... */
  { "/" N_("_Reports"),					NULL,
	NULL,			0,					"<Branch>"	},
  { "/" N_("Reports") "/tearoff1",			NULL,
	NULL,			0,					"<Tearoff>"	},
  { "/" N_("Reports") "/" N_("_View"),			"F1",
	reports_menu_callback,	MENU_REPORT_MAP_VIEW					},
  { "/" N_("Reports") "/" N_("_Units"),			"F2",
	reports_menu_callback,	MENU_REPORT_UNITS					},
  { "/" N_("Reports") "/" N_("_Nations"),		"F3",
	reports_menu_callback,	MENU_REPORT_PLAYERS					},
  { "/" N_("Reports") "/" N_("_Cities"),		"F4",
	reports_menu_callback,	MENU_REPORT_CITIES					},
  { "/" N_("Reports") "/" N_("_Economy"),		"F5",
	reports_menu_callback,	MENU_REPORT_ECONOMY					},
  { "/" N_("Reports") "/" N_("_Research"),		"F6",
	reports_menu_callback,	MENU_REPORT_SCIENCE					},
  { "/" N_("Reports") "/sep1",				NULL,
	NULL,			0,					"<Separator>"	},
  { "/" N_("Reports") "/" N_("_Wonders of the World"),	"F7",
	reports_menu_callback,	MENU_REPORT_WOW						},
  { "/" N_("Reports") "/" N_("_Top Five Cities"),	"F8",
	reports_menu_callback,	MENU_REPORT_TOP_CITIES					},
  { "/" N_("Reports") "/" N_("_Messages"),		"F9",
	reports_menu_callback,	MENU_REPORT_MESSAGES					},
  { "/" N_("Reports") "/" N_("_Demographics"),		"F11",
	reports_menu_callback,	MENU_REPORT_DEMOGRAPHIC					},
  { "/" N_("Reports") "/" N_("_Spaceship"),		"F12",
	reports_menu_callback,	MENU_REPORT_SPACESHIP					},

  /* Help menu ... */
  { "/" N_("_Help"),					NULL,
	NULL,			0,					"<Branch>"	},
  { "/" N_("Help") "/tearoff1",				NULL,
	NULL,			0,					"<Tearoff>"	},
  { "/" N_("Help") "/" N_("Language_s"),		NULL,
	help_menu_callback,	MENU_HELP_LANGUAGES					},
  { "/" N_("Help") "/" N_("Co_nnecting"),		NULL,
	help_menu_callback,	MENU_HELP_CONNECTING					},
  { "/" N_("Help") "/" N_("C_ontrols"),			NULL,
	help_menu_callback,	MENU_HELP_CONTROLS					},
  { "/" N_("Help") "/" N_("C_hatline"),			NULL,
	help_menu_callback,	MENU_HELP_CHATLINE					},
  { "/" N_("Help") "/" N_("_Worklist Editor"),			NULL,
	help_menu_callback,	MENU_HELP_WORKLIST_EDITOR				},
  { "/" N_("Help") "/" N_("Citizen _Management"),			NULL,
	help_menu_callback,	MENU_HELP_CMA						},
  { "/" N_("Help") "/" N_("_Playing"),			NULL,
	help_menu_callback,	MENU_HELP_PLAYING					},
  { "/" N_("Help") "/sep1",				NULL,
	NULL,			0,					"<Separator>"	},
  { "/" N_("Help") "/" N_("City _Improvements"),        NULL,
	help_menu_callback,	MENU_HELP_IMPROVEMENTS					},
  { "/" N_("Help") "/" N_("_Units"),			NULL,
	help_menu_callback,	MENU_HELP_UNITS						},
  { "/" N_("Help") "/" N_("Com_bat"),			NULL,
	help_menu_callback,	MENU_HELP_COMBAT					},
  { "/" N_("Help") "/" N_("_ZOC"),			NULL,
	help_menu_callback,	MENU_HELP_ZOC						},
  { "/" N_("Help") "/" N_("Techno_logy"),		NULL,
	help_menu_callback,	MENU_HELP_TECH						},
  { "/" N_("Help") "/" N_("_Terrain"),			NULL,
	help_menu_callback,	MENU_HELP_TERRAIN					},
  { "/" N_("Help") "/" N_("Won_ders"),			NULL,
	help_menu_callback,	MENU_HELP_WONDERS					},
  { "/" N_("Help") "/" N_("_Government"),		NULL,
	help_menu_callback,	MENU_HELP_GOVERNMENT					},
  { "/" N_("Help") "/" N_("Happin_ess"),		NULL,
	help_menu_callback,	MENU_HELP_HAPPINESS					},
  { "/" N_("Help") "/" N_("Space _Race"),		NULL,
	help_menu_callback,	MENU_HELP_SPACE_RACE					},
  { "/" N_("Help") "/sep2",				NULL,
	NULL,			0,					"<Separator>"	},
  { "/" N_("Help") "/" N_("_Copying"),			NULL,
	help_menu_callback,	MENU_HELP_COPYING					},
  { "/" N_("Help") "/" N_("_About"),			NULL,
	help_menu_callback,	MENU_HELP_ABOUT						}
};

#ifdef ENABLE_NLS
/****************************************************************
  gettext-translates each "/" delimited component of menu path,
  puts them back together, and returns as a static string.
  Any component which is of form "<foo>" is _not_ translated.

  Path should include underscores like in the menu itself.
*****************************************************************/
static char *menu_path_tok(char *path)
{
  bool escaped = FALSE;

  while (*path) {
    if (escaped) {
      escaped = FALSE;
    } else {
      if (*path == '\\') {
        escaped = TRUE;
      } else if (*path == '/') {
        *path = '\0';
        return path;
      }
    }

    path++;
  }

  return NULL;
}
#endif

/****************************************************************
...
*****************************************************************/
static gchar *translate_func(const gchar *path, gpointer data)
{
#ifndef ENABLE_NLS
    static gchar res[100];
    
    g_strlcpy(res, path, sizeof(res));
#else
    static struct astring in, out, tmp;   /* these are never free'd */
    char *tok, *next, *t;
    const char *trn;
    int len;
    char *res;

    /* copy to in so can modify with menu_path_tok: */
    astr_minsize(&in, strlen(path)+1);
    strcpy(in.str, path);
    astr_minsize(&out, 1);
    out.str[0] = '\0';
    freelog(LOG_DEBUG, "trans: %s", in.str);

    tok = in.str;
    do {
      next = menu_path_tok(tok);

      len = strlen(tok);
      freelog(LOG_DEBUG, "tok \"%s\", len %d", tok, len);
      if (len == 0 || (tok[0] == '<' && tok[len-1] == '>')) {
	t = tok;
      } else {
	trn = _(tok);
	len = strlen(trn) + 1;	/* string plus leading '/' */
	astr_minsize(&tmp, len+1);
	sprintf(tmp.str, "/%s", trn);
	t = tmp.str;
	len = strlen(t);
      }
      astr_minsize(&out, out.n + len);
      strcat(out.str, t);
      freelog(LOG_DEBUG, "t \"%s\", len %d, out \"%s\"", t, len, out.str);
      tok = next+1;
    } while (next);
    res = out.str;
#endif
  
  return res;
}

/****************************************************************
...
*****************************************************************/
static const char *menu_path_remove_uline(const char *path)
{
  static char res[100];
  const char *from;
  char *to;
  
  from = path;
  to = res;

  do {
    if (*from != '_') {
      *(to++) = *from;
    }
  } while (*(from++));

  return res;
}

/****************************************************************
  ...
 *****************************************************************/
void setup_menus(GtkWidget *window, GtkWidget **menubar)
{
  const int nmenu_items = ARRAY_SIZE(menu_items);

  toplevel_accel = gtk_accel_group_new();
  item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<main>",
      toplevel_accel);
  gtk_item_factory_set_translate_func(item_factory, translate_func,
      NULL, NULL);

  gtk_accel_group_lock(toplevel_accel);
  gtk_item_factory_create_items(item_factory, nmenu_items, menu_items, NULL);
  gtk_window_add_accel_group(GTK_WINDOW(window), toplevel_accel);

  main_menubar = gtk_item_factory_get_widget(item_factory, "<main>");
  g_signal_connect(main_menubar, "destroy",
      G_CALLBACK(gtk_widget_destroyed), &main_menubar);

  if (menubar) {
    *menubar = main_menubar;
  }
}

/****************************************************************
...
*****************************************************************/
static void menus_set_sensitive(const char *path, int sensitive)
{
  GtkWidget *item;

  path = menu_path_remove_uline(path);

  if(!(item = gtk_item_factory_get_item(item_factory, path))) {
    freelog(LOG_ERROR,
	    "Can't set sensitivity for non-existent menu %s.", path);
    return;
  }

  gtk_widget_set_sensitive(item, sensitive);
}

/****************************************************************
  Sets the toggled state on the check menu item given by 'path'
  according to 'active', without the associated callback being
  called.
*****************************************************************/
static void menus_set_active_no_callback(const gchar *path,
                                         gboolean active)
{
  GtkWidget *w, *item;
  guint sid;
  gulong hid;

  path = menu_path_remove_uline(path);

  if (!(item = gtk_item_factory_get_item(item_factory, path))) {
    freelog(LOG_ERROR, "Can't set active for non-existent menu %s.",
            path);
    return;
  }

  if (!(w = gtk_item_factory_get_widget(item_factory, path))) {
    freelog(LOG_ERROR, "Can't set active for non-existent menu %s.",
            path);
    return;
  }

  sid = g_signal_lookup("activate", G_TYPE_FROM_INSTANCE(w));
  if (sid == 0) {
    freelog(LOG_ERROR, "Can't block menu callback because "
            "the \"activate\" signal id was not found for "
            "the menu widget at path \"%s\".", path);
    return;
  }

  hid = g_signal_handler_find(w, G_SIGNAL_MATCH_ID,
                              sid, 0, NULL, NULL, NULL);

  if (hid == 0) {
    freelog(LOG_ERROR, "Can't block menu callback because "
            "the \"activate\" signal handler id was not found "
            "for the menu widget at path \"%s\".", path);
    return;
  }

  g_signal_handler_block(w, hid);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), active);
  g_signal_handler_unblock(w, hid);
}

/****************************************************************
...
*****************************************************************/
static void menus_set_active(const char *path, int active)
{
  GtkWidget *item;

  path = menu_path_remove_uline(path);

  if (!(item = gtk_item_factory_get_item(item_factory, path))) {
    freelog(LOG_ERROR,
	    "Can't set active for non-existent menu %s.", path);
    return;
  }

  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), active);
}

#ifdef UNUSED 
/****************************************************************
...
*****************************************************************/
static void menus_set_shown(const char *path, int shown)
{
  GtkWidget *item;
  
  path = menu_path_remove_uline(path);
  
  if(!(item = gtk_item_factory_get_item(item_factory, path))) {
    freelog(LOG_ERROR, "Can't show non-existent menu %s.", path);
    return;
  }

  if (shown) {
    gtk_widget_show(item);
  } else {
    gtk_widget_hide(item);
  }
}
#endif /* UNUSED */

/****************************************************************
...
*****************************************************************/
static void menus_rename(const char *path, const char *s)
{
  GtkWidget *item;

  path = menu_path_remove_uline(path);

  if(!(item = gtk_item_factory_get_item(item_factory, path))) {
    freelog(LOG_ERROR, "Can't rename non-existent menu %s.", path);
    return;
  }

  gtk_label_set_text_with_mnemonic(GTK_LABEL(GTK_BIN(item)->child), s);
}

/****************************************************************
  The player has chosen a government from the menu.
*****************************************************************/
static void government_callback(GtkMenuItem *item, gpointer data)
{
  struct government *gov = data;

  popup_revolution_dialog(gov);
}

/****************************************************************************
  Return the text for the tile, changed by the activity.

  Should only be called for irrigation, mining, or transformation, and
  only when the activity changes the base terrain type.
****************************************************************************/
static const char *get_tile_change_menu_text(struct tile *ptile,
					     enum unit_activity activity)
{
  struct tile newtile = *ptile;

  tile_apply_activity(&newtile, activity);
  return tile_get_info_text(&newtile, 0);
}

/****************************************************************
Note: the menu strings should contain underscores as in the
menu_items struct. The underscores will be removed elsewhere if
the string is used for a lookup via gtk_item_factory_get_widget()
*****************************************************************/
void update_menus(void)
{
  if (!main_menubar) {
    return;
  }

  menus_set_active("<main>/_Game/_Options/Save Options on _Exit",
		   save_options_on_exit);
  menus_set_sensitive("<main>/_Game/_Options/_Remote Server", 
		      client.conn.established);

  menus_set_sensitive("<main>/_Game/Save Game _As...",
		      can_client_access_hack()
		      && C_S_RUNNING <= client_state());
  menus_set_sensitive("<main>/_Game/_Save Game",
		      can_client_access_hack()
		      && C_S_RUNNING <= client_state());
  menus_set_sensitive("<main>/_Game/_Leave",
		      client.conn.established);

  if (!can_client_change_view()) {
    menus_set_sensitive("<main>/_Edit", FALSE);
    menus_set_sensitive("<main>/_View", FALSE);
    menus_set_sensitive("<main>/_Orders", FALSE);
    menus_set_sensitive("<main>/_Reports", FALSE);
  } else {
    const char *path =
      menu_path_remove_uline("<main>/_Game/_Government");
    GtkWidget *parent = gtk_item_factory_get_widget(item_factory, path);

    if (parent) {
      GList *list, *iter;

      /* remove previous government entries. */
      list = gtk_container_get_children(GTK_CONTAINER(parent));
      for (iter = g_list_nth(list, 2); iter; iter = g_list_next(iter)) {
	gtk_widget_destroy(GTK_WIDGET(iter->data));
      }
      g_list_free(list);

      /* add new government entries. */
      government_iterate(g) {
        if (g != game.government_when_anarchy) {
          GtkWidget *item, *image;
          struct sprite *gsprite;
	  char buf[256];

	  my_snprintf(buf, sizeof(buf), _("%s..."),
		      government_name_translation(g));
          item = gtk_image_menu_item_new_with_label(buf);

	  if ((gsprite = get_government_sprite(tileset, g))) {
	    image = gtk_image_new_from_pixbuf(sprite_get_pixbuf(gsprite));
	    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);
	    gtk_widget_show(image);
	  }

          g_signal_connect(item, "activate",
			   G_CALLBACK(government_callback), g);

          if (!can_change_to_government(client.conn.playing, g)) {
            gtk_widget_set_sensitive(item, FALSE);
	  }

          gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
          gtk_widget_show(item);
        }
      } government_iterate_end;
    }

    menus_set_sensitive("<main>/_Reports", TRUE);
    menus_set_sensitive("<main>/_Edit", TRUE);
    menus_set_sensitive("<main>/_View", TRUE);
    menus_set_sensitive("<main>/_Orders", can_client_issue_orders());

    menus_set_sensitive("<main>/_Game/_Government",
			can_client_issue_orders());
    menus_set_sensitive("<main>/_Game/_Government/Tax Rates",
			game.info.changable_tax
                        && can_client_issue_orders());

    menus_set_sensitive("<main>/_Edit/Worklists",
			can_client_issue_orders());
    menus_set_active_no_callback("<main>/_Edit/Editing _Mode",
                                 game.info.is_edit_mode);
    menus_set_sensitive("<main>/_Edit/Editing _Mode",
                        can_conn_enable_editing(&client.conn));
    menus_set_sensitive("<main>/_Edit/Recalculate _Borders",
			can_conn_edit(&client.conn));
    menus_set_sensitive("<main>/_Edit/Regenerate _Water",
			can_conn_edit(&client.conn));
    menus_set_sensitive("<main>/_Edit/Toggle Fog-of-war",
			can_conn_edit(&client.conn));

    editgui_refresh();

    /* If the client is not attached to a player, disable these reports. */
    menus_set_sensitive("<main>/_Reports/_Cities",
			(NULL != client.conn.playing));
    menus_set_sensitive("<main>/_Reports/_Units",
			(NULL != client.conn.playing));
    menus_set_sensitive("<main>/_Reports/_Economy",
			(NULL != client.conn.playing));
    menus_set_sensitive("<main>/_Reports/_Research",
			(NULL != client.conn.playing));
    menus_set_sensitive("<main>/_Reports/_Demographics",
			(NULL != client.conn.playing));
    menus_set_sensitive("<main>/_Reports/_Spaceship",
			(NULL != client.conn.playing
			 && SSHIP_NONE != client.conn.playing->spaceship.state));

    menus_set_active("<main>/_View/City Outlines", draw_city_outlines);
    menus_set_active("<main>/_View/Map _Grid", draw_map_grid);
    menus_set_sensitive("<main>/_View/National _Borders", game.info.borders > 0);
    menus_set_active("<main>/_View/National _Borders", draw_borders);
    menus_set_active("<main>/_View/City _Names", draw_city_names);

    /* The "full" city bar (i.e. the new way of drawing the
     * city name), can draw the city growth even without drawing
     * the city name. But the old method cannot. */
    if (draw_full_citybar) {
      menus_set_sensitive("<main>/_View/City G_rowth", TRUE);
    } else {
      menus_set_sensitive("<main>/_View/City G_rowth", draw_city_names);
    }

    menus_set_active("<main>/_View/City G_rowth", draw_city_growth);
    menus_set_active("<main>/_View/City _Productions", draw_city_productions);
    menus_set_active("<main>/_View/Terrain", draw_terrain);
    menus_set_active("<main>/_View/Coastline", draw_coastline);
    menus_set_sensitive("<main>/_View/Coastline", !draw_terrain);
    menus_set_active("<main>/_View/Improvements/Roads & Rails", draw_roads_rails);
    menus_set_active("<main>/_View/Improvements/Irrigation", draw_irrigation);
    menus_set_active("<main>/_View/Improvements/Mines", draw_mines);
    menus_set_active("<main>/_View/Improvements/Fortress & Airbase", draw_fortress_airbase);
    menus_set_active("<main>/_View/Specials", draw_specials);
    menus_set_active("<main>/_View/Pollution & Fallout", draw_pollution);
    menus_set_active("<main>/_View/Cities", draw_cities);
    menus_set_active("<main>/_View/Units", draw_units);
    menus_set_active("<main>/_View/Focus Unit", draw_focus_unit);
    menus_set_sensitive("<main>/_View/Focus Unit", !draw_units);
    menus_set_active("<main>/_View/Fog of War", draw_fog_of_war);

    menus_set_active("<main>/_View/_Full Screen", fullscreen_mode);

    /* Remaining part of this function: Update Orders menu */

    if (!can_client_issue_orders()) {
      return;
    }

    if (get_num_units_in_focus() > 0) {
      const char *irrfmt = _("Change to %s (_I)");
      const char *minfmt = _("Change to %s (_M)");
      const char *transfmt = _("Transf_orm to %s");
      char irrtext[128], mintext[128], transtext[128];
      const char *roadtext;
      struct terrain *pterrain;
      struct unit_list *punits = get_units_in_focus();

      sz_strlcpy(irrtext, _("Build _Irrigation"));
      sz_strlcpy(mintext, _("Build _Mine"));
      sz_strlcpy(transtext, _("Transf_orm Terrain"));
      
      /* Enable the button for adding to a city in all cases, so we
	 get an eventual error message from the server if we try. */
      menus_set_sensitive("<main>/_Orders/_Build City",
		(can_units_do(punits, can_unit_add_or_build_city)
		 || can_units_do(punits, unit_can_help_build_wonder_here)));
      menus_set_sensitive("<main>/_Orders/Build _Road",
                          (can_units_do_activity(punits, ACTIVITY_ROAD)
                           || can_units_do_activity(punits, ACTIVITY_RAILROAD)
                           || can_units_do(punits,
					   unit_can_est_traderoute_here)));
      menus_set_sensitive("<main>/_Orders/Build _Irrigation",
                          can_units_do_activity(punits, ACTIVITY_IRRIGATE));
      menus_set_sensitive("<main>/_Orders/Build _Mine",
                          can_units_do_activity(punits, ACTIVITY_MINE));
      menus_set_sensitive("<main>/_Orders/Transf_orm Terrain",
			  can_units_do_activity(punits, ACTIVITY_TRANSFORM));
      menus_set_sensitive("<main>/_Orders/Build _Fortress",
                          (can_units_do_base_gui(punits, BASE_GUI_FORTRESS)
                           || can_units_do_activity(punits,
						    ACTIVITY_FORTIFYING)));
      menus_set_sensitive("<main>/_Orders/Build Airbas_e",
			  can_units_do_base_gui(punits, BASE_GUI_AIRBASE));
      menus_set_sensitive("<main>/_Orders/Clean _Pollution",
                          (can_units_do_activity(punits, ACTIVITY_POLLUTION)
                           || can_units_do(punits, can_unit_paradrop)));
      menus_set_sensitive("<main>/_Orders/Clean _Nuclear Fallout",
			  can_units_do_activity(punits, ACTIVITY_FALLOUT));
      menus_set_sensitive("<main>/_Orders/_Sentry",
			  can_units_do_activity(punits, ACTIVITY_SENTRY));
      menus_set_sensitive("<main>/_Orders/Pillage",
			  can_units_do_activity(punits, ACTIVITY_PILLAGE));
      menus_set_sensitive("<main>/_Orders/_Disband Unit",
			  units_have_flag(punits, F_UNDISBANDABLE, FALSE));
      menus_set_sensitive("<main>/_Orders/_Upgrade unit",
			  TRUE /* FIXME: what check should we do? */);
      menus_set_sensitive("<main>/_Orders/Make _Homecity",
			  can_units_do(punits, can_unit_change_homecity));
      menus_set_sensitive("<main>/_Orders/_Unload Transporter",
			  units_are_occupied(punits));
      menus_set_sensitive("<main>/_Orders/_Load",
			  units_can_load(punits));
      menus_set_sensitive("<main>/_Orders/_Unload",
			  units_can_unload(punits));
      menus_set_sensitive("<main>/_Orders/Wake up o_thers", 
			  units_have_activity_on_tile(punits,
						      ACTIVITY_SENTRY));
      menus_set_sensitive("<main>/_Orders/_Auto Settler",
                          can_units_do(punits, can_unit_do_autosettlers));
      menus_set_sensitive("<main>/_Orders/Auto E_xplore",
                          can_units_do_activity(punits, ACTIVITY_EXPLORE));
      menus_set_sensitive("<main>/_Orders/_Connect/_Road",
                          can_units_do_connect(punits, ACTIVITY_ROAD));
      menus_set_sensitive("<main>/_Orders/_Connect/_Rail",
                          can_units_do_connect(punits, ACTIVITY_RAILROAD));
      menus_set_sensitive("<main>/_Orders/_Connect/_Irrigate",
                          can_units_do_connect(punits, ACTIVITY_IRRIGATE));
      menus_set_sensitive("<main>/_Orders/Diplomat\\/Spy Actions",
			  can_units_do_diplomat_action(punits,
						       DIPLOMAT_ANY_ACTION));
      menus_set_sensitive("<main>/_Orders/Explode Nuclear",
			  units_have_flag(punits, F_NUCLEAR, TRUE));

      if (units_have_flag(punits, F_HELP_WONDER, TRUE)) {
        menus_rename("<main>/_Orders/_Build City", _("Help _Build Wonder"));
      } else {
        bool city_on_tile = FALSE;

        /* FIXME: this overloading doesn't work well with multiple focus
         * units. */
        unit_list_iterate(punits, punit) {
          if (tile_city(punit->tile)) {
            city_on_tile = TRUE;
            break;
          }
        } unit_list_iterate_end;
        
        if (city_on_tile && units_have_flag(punits, F_ADD_TO_CITY, TRUE)) {
          menus_rename("<main>/_Orders/_Build City", _("Add to City (_B)"));
        } else {
          /* refresh default order */
          menus_rename("<main>/_Orders/_Build City", _("_Build City"));
        }
      }
 
      if (units_have_flag(punits, F_TRADE_ROUTE, TRUE))
	menus_rename("<main>/_Orders/Build _Road", _("Make Trade _Route"));
      else if (units_have_flag(punits, F_SETTLERS, TRUE)) {
	bool has_road = FALSE;

	/* FIXME: this overloading doesn't work well with multiple focus
	 * units. */
	unit_list_iterate(punits, punit) {
	  if (tile_has_special(punit->tile, S_ROAD)) {
	    has_road = TRUE;
	    break;
	  }
	} unit_list_iterate_end;

	if (has_road) {
	  roadtext = _("Build _Railroad");
	  road_activity=ACTIVITY_RAILROAD;  
	} else {
	  roadtext = _("Build _Road");
	  road_activity=ACTIVITY_ROAD;  
	}
	menus_rename("<main>/_Orders/Build _Road", roadtext);
      }
      else
	menus_rename("<main>/_Orders/Build _Road", _("Build _Road"));

      if (unit_list_size(punits) == 1) {
	struct unit *punit = unit_list_get(punits, 0);

	pterrain = tile_terrain(punit->tile);
	if (pterrain->irrigation_result != T_NONE
	    && pterrain->irrigation_result != pterrain) {
	  my_snprintf(irrtext, sizeof(irrtext), irrfmt,
		      get_tile_change_menu_text(punit->tile,
						ACTIVITY_IRRIGATE));
	} else if (tile_has_special(punit->tile, S_IRRIGATION)
		   && player_knows_techs_with_flag(unit_owner(punit),
						   TF_FARMLAND)) {
	  sz_strlcpy(irrtext, _("Bu_ild Farmland"));
	}
	if (pterrain->mining_result != T_NONE
	    && pterrain->mining_result != pterrain) {
	  my_snprintf(mintext, sizeof(mintext), minfmt,
		      get_tile_change_menu_text(punit->tile, ACTIVITY_MINE));
	}
	if (pterrain->transform_result != T_NONE
	    && pterrain->transform_result != pterrain) {
	  my_snprintf(transtext, sizeof(transtext), transfmt,
		      get_tile_change_menu_text(punit->tile,
						ACTIVITY_TRANSFORM));
	}
      }

      menus_rename("<main>/_Orders/Build _Irrigation", irrtext);
      menus_rename("<main>/_Orders/Build _Mine", mintext);
      menus_rename("<main>/_Orders/Transf_orm Terrain", transtext);

      if (can_units_do_activity(punits, ACTIVITY_FORTIFYING)) {
	menus_rename("<main>/_Orders/Build _Fortress", _("_Fortify"));
      } else {
	menus_rename("<main>/_Orders/Build _Fortress", _("Build _Fortress"));
      }

      if (units_have_flag(punits, F_PARATROOPERS, TRUE)) {
	menus_rename("<main>/_Orders/Clean _Pollution", _("_Paradrop"));
      } else {
	menus_rename("<main>/_Orders/Clean _Pollution", _("Clean _Pollution"));
      }

      menus_set_sensitive("<main>/_Orders", TRUE);
    } else {
      menus_set_sensitive("<main>/_Orders", FALSE);
    }
  }
}
