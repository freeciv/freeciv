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
#include "capability.h"
#include "fcintl.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "support.h"
#include "unit.h"

#include "chatline.h"
#include "cityrep.h"
#include "civclient.h"
#include "clinet.h"
#include "control.h"
#include "dialogs.h"
#include "finddlg.h"
#include "gotodlg.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "mapctrl.h"   /* center_on_unit */
#include "messagedlg.h"
#include "messagewin.h"
#include "optiondlg.h"
#include "options.h"
#include "plrdlg.h"
#include "ratesdlg.h"
#include "repodlgs.h"
#include "spaceshipdlg.h"

#include "menu.h"

GtkItemFactory *item_factory=NULL;


/****************************************************************
...
*****************************************************************/
enum unit_activity road_activity;

enum MenuID {
  MENU_END_OF_LIST=0,

  MENU_GAME_OPTIONS,
  MENU_GAME_MSG_OPTIONS,
  MENU_GAME_SAVE_SETTINGS,
  MENU_GAME_PLAYERS,
  MENU_GAME_MESSAGES,
  MENU_GAME_SERVER_OPTIONS1,
  MENU_GAME_SERVER_OPTIONS2,
  MENU_GAME_OUTPUT_LOG,
  MENU_GAME_CLEAR_OUTPUT,
  MENU_GAME_DISCONNECT,
  MENU_GAME_QUIT,
  
  MENU_KINGDOM_TAX_RATE,
  MENU_KINGDOM_FIND_CITY,
  MENU_KINGDOM_REVOLUTION,

  MENU_VIEW_SHOW_MAP_GRID,
  MENU_VIEW_CENTER_VIEW,

  MENU_ORDER_AUTO_SETTLER,
  MENU_ORDER_AUTO_ATTACK,
  MENU_ORDER_MINE,
  MENU_ORDER_IRRIGATE,
  MENU_ORDER_TRANSFORM,
  MENU_ORDER_FORTRESS,
  MENU_ORDER_AIRBASE,
  MENU_ORDER_BUILD_CITY,
  MENU_ORDER_ROAD,
  MENU_ORDER_CONNECT,
  MENU_ORDER_POLLUTION,
  MENU_ORDER_FORTIFY,
  MENU_ORDER_SENTRY,
  MENU_ORDER_PILLAGE,
  MENU_ORDER_AUTO_EXPLORE,
  MENU_ORDER_HOMECITY,
  MENU_ORDER_WAIT,
  MENU_ORDER_UNLOAD,
  MENU_ORDER_WAKEUP_OTHERS,
  MENU_ORDER_GOTO,
  MENU_ORDER_GOTO_CITY,
  MENU_ORDER_DISBAND,
  MENU_ORDER_BUILD_WONDER,
  MENU_ORDER_TRADEROUTE,
  MENU_ORDER_DONE,
  MENU_ORDER_NUKE,

  MENU_REPORT_CITY,
  MENU_REPORT_SCIENCE,
  MENU_REPORT_TRADE,
  MENU_REPORT_MILITARY,
  MENU_REPORT_DEMOGRAPHIC,
  MENU_REPORT_TOP_CITIES,
  MENU_REPORT_WOW,
  MENU_REPORT_SPACESHIP,

  MENU_HELP_CONTROLS,
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
  MENU_HELP_ABOUT,
  MENU_HELP_CONNECTING,
  MENU_HELP_CHATLINE,
  MENU_HELP_LANGUAGES
};


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
  case MENU_GAME_SAVE_SETTINGS:
    save_options();
    break;
  case MENU_GAME_PLAYERS:
    popup_players_dialog();
    break;
  case MENU_GAME_MESSAGES:
    popup_meswin_dialog();
    break;
  case MENU_GAME_SERVER_OPTIONS1:
    send_report_request(REPORT_SERVER_OPTIONS1);
    break;
  case MENU_GAME_SERVER_OPTIONS2:
    send_report_request(REPORT_SERVER_OPTIONS2);
    break;
  case MENU_GAME_OUTPUT_LOG:
    log_output_window();
    break;
  case MENU_GAME_CLEAR_OUTPUT:
    clear_output_window();
    break;
  case MENU_GAME_DISCONNECT:
    disconnect_from_server();
    break;
  case MENU_GAME_QUIT:
    exit(0);
    break;
  }
}


/****************************************************************
...
*****************************************************************/
static void kingdom_menu_callback(gpointer callback_data,
				  guint callback_action, GtkWidget *widget)
{
  switch(callback_action) {
  case MENU_KINGDOM_TAX_RATE:
    popup_rates_dialog();
    break;
  case MENU_KINGDOM_FIND_CITY:
    popup_find_dialog();
    break;
  case MENU_KINGDOM_REVOLUTION:
    popup_revolution_dialog();
    break;
  }
}


/****************************************************************
...
*****************************************************************/
static void view_menu_callback(gpointer callback_data, guint callback_action,
			       GtkWidget *widget)
{
  switch(callback_action) {
  case MENU_VIEW_SHOW_MAP_GRID:
    key_map_grid_toggle ();
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
   case MENU_ORDER_AUTO_SETTLER:
   case MENU_ORDER_AUTO_ATTACK:
    if(get_unit_in_focus())
      request_unit_auto(get_unit_in_focus());
    break;
   case MENU_ORDER_BUILD_CITY:
    if(get_unit_in_focus())
      request_unit_build_city(get_unit_in_focus());
    break;
   case MENU_ORDER_IRRIGATE:
    if(get_unit_in_focus())
      request_new_unit_activity(get_unit_in_focus(), ACTIVITY_IRRIGATE);
    break;
   case MENU_ORDER_FORTRESS:
    if(get_unit_in_focus())
      request_new_unit_activity(get_unit_in_focus(), ACTIVITY_FORTRESS);
    break;
   case MENU_ORDER_AIRBASE:
    key_unit_airbase(); 
    break;
   case MENU_ORDER_MINE:
    if(get_unit_in_focus())
      request_new_unit_activity(get_unit_in_focus(), ACTIVITY_MINE);
    break;
   case MENU_ORDER_TRANSFORM:
    if(get_unit_in_focus())
      request_new_unit_activity(get_unit_in_focus(), ACTIVITY_TRANSFORM);
    break;
   case MENU_ORDER_ROAD:
    if(get_unit_in_focus())
      request_new_unit_activity(get_unit_in_focus(), road_activity);
    break;
   case MENU_ORDER_CONNECT:
    if(get_unit_in_focus())
      request_unit_connect();
    break;
   case MENU_ORDER_POLLUTION:
    key_unit_pollution();
    break;
   case MENU_ORDER_HOMECITY:
    if(get_unit_in_focus())
      request_unit_change_homecity(get_unit_in_focus());
    break;
   case MENU_ORDER_FORTIFY:
    if(get_unit_in_focus())
      request_new_unit_activity(get_unit_in_focus(), ACTIVITY_FORTIFY);
    break;
   case MENU_ORDER_SENTRY:
    if(get_unit_in_focus())
      request_new_unit_activity(get_unit_in_focus(), ACTIVITY_SENTRY);
    break;
   case MENU_ORDER_WAIT:
    if(get_unit_in_focus())
      request_unit_wait(get_unit_in_focus());
    break;
   case MENU_ORDER_UNLOAD:
    if(get_unit_in_focus())
      request_unit_unload(get_unit_in_focus());
    break;
   case MENU_ORDER_WAKEUP_OTHERS:
    if(get_unit_in_focus())
      request_unit_wakeup(get_unit_in_focus());
    break;
   case MENU_ORDER_GOTO:
    if(get_unit_in_focus())
      request_unit_goto();
    break;
   case MENU_ORDER_GOTO_CITY:
    if(get_unit_in_focus())
      popup_goto_dialog();
    break;
   case MENU_ORDER_DISBAND:
    if(get_unit_in_focus())
      request_unit_disband(get_unit_in_focus());
    break;
   case MENU_ORDER_PILLAGE:
    if(get_unit_in_focus())
      request_unit_pillage(get_unit_in_focus());
     break;
   case MENU_ORDER_AUTO_EXPLORE:
    if(get_unit_in_focus())
      request_new_unit_activity(get_unit_in_focus(), ACTIVITY_EXPLORE);
     break;
   case MENU_ORDER_BUILD_WONDER:
    if(get_unit_in_focus())
      request_unit_caravan_action(get_unit_in_focus(),
				 PACKET_UNIT_HELP_BUILD_WONDER);
     break;
   case MENU_ORDER_TRADEROUTE:
    if(get_unit_in_focus())
      request_unit_caravan_action(get_unit_in_focus(),
				 PACKET_UNIT_ESTABLISH_TRADE);
     break;
   case MENU_ORDER_DONE:
    if(get_unit_in_focus())
      request_unit_move_done();
    break;
   case MENU_ORDER_NUKE:
    if(get_unit_in_focus())
      request_unit_nuke(get_unit_in_focus());
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
   case MENU_REPORT_CITY:
    popup_city_report_dialog(0);
    break;
   case MENU_REPORT_SCIENCE:
    popup_science_dialog(0);
    break;
   case MENU_REPORT_TRADE:
    popup_trade_report_dialog(0);
    break;
   case MENU_REPORT_MILITARY:
    popup_activeunits_report_dialog(0);
    break;
  case MENU_REPORT_DEMOGRAPHIC:
    send_report_request(REPORT_DEMOGRAPHIC);
    break;
  case MENU_REPORT_TOP_CITIES:
    send_report_request(REPORT_TOP_5_CITIES);
    break;
   case MENU_REPORT_WOW:
    send_report_request(REPORT_WONDERS_OF_THE_WORLD);
    break;
   case MENU_REPORT_SPACESHIP:
    popup_spaceship_dialog(game.player_ptr);
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
  case MENU_HELP_CHATLINE:
    popup_help_dialog_string(HELP_CHATLINE_ITEM);
    break;
  case MENU_HELP_CONTROLS:
    popup_help_dialog_string(HELP_CONTROLS_ITEM);
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
 */
static GtkItemFactoryEntry menu_items[]	=
{
  { "/" N_("_Game"),			NULL,		NULL,
    0,					"<Branch>"			      },
  { "/" N_("Game") "/tearoff1",		NULL,		NULL,
    0,					"<Tearoff>"			      },
  { "/" N_("Game") "/sep1",		NULL,		NULL,
    0,					"<Separator>"			      },
  { "/" N_("Game") "/" N_("Local Options"),	NULL,		game_menu_callback,
    MENU_GAME_OPTIONS							      },
  { "/" N_("Game") "/" N_("Message Options"),	NULL,		game_menu_callback,
    MENU_GAME_MSG_OPTIONS						      },
  { "/" N_("Game") "/" N_("Save Settings"),	NULL,		game_menu_callback,
    MENU_GAME_SAVE_SETTINGS						      },
  { "/" N_("Game") "/sep2",		NULL,		NULL,
    0,					"<Separator>"			      },
  { "/" N_("Game") "/" N_("Players"),		"F3",		game_menu_callback,
    MENU_GAME_PLAYERS							      },
  { "/" N_("Game") "/" N_("Messages"),		"F10",		game_menu_callback,
    MENU_GAME_MESSAGES							      },
  { "/" N_("Game") "/sep3",		NULL,		NULL,
    0,					"<Separator>"			      },
  { "/" N_("Game") "/" N_("Server Opt initial"), NULL,	game_menu_callback,
    MENU_GAME_SERVER_OPTIONS1						      },
  { "/" N_("Game") "/" N_("Server Opt ongoing"), NULL,	game_menu_callback,
    MENU_GAME_SERVER_OPTIONS2						      },
  { "/" N_("Game") "/sep4",		NULL,		NULL,
    0,					"<Separator>"			      },
  { "/" N_("Game") "/" N_("Export Log"),	NULL,		game_menu_callback,
    MENU_GAME_OUTPUT_LOG						      },
  { "/" N_("Game") "/" N_("Clear Log"),	NULL,		game_menu_callback,
    MENU_GAME_CLEAR_OUTPUT						      },
  { "/" N_("Game") "/sep5",		NULL,		NULL,
    0,					"<Separator>"			      },
  { "/" N_("Game") "/" N_("Disconnect"),	NULL,		game_menu_callback,
    MENU_GAME_DISCONNECT						      },
  { "/" N_("Game") "/" N_("Quit"),		"<control>q",   gtk_main_quit,
    0									      },

  { "/" N_("_Kingdom"),			NULL,		NULL,
    0,					"<Branch>"			      },
  { "/" N_("Kingdom") "/tearoff1",		NULL,		NULL,
    0,					"<Tearoff>"			      },
  { "/" N_("Kingdom") "/" N_("_Tax Rates"),	"<shift>t",	kingdom_menu_callback,
    MENU_KINGDOM_TAX_RATE						      },
  { "/" N_("Kingdom") "/sep1",		NULL,		NULL,
    0,					"<Separator>"			      },
  { "/" N_("Kingdom") "/" N_("_Find City"),	"<control>f",	kingdom_menu_callback,
    MENU_KINGDOM_FIND_CITY							      },
  { "/" N_("Kingdom") "/sep2",		NULL,		NULL,
    0,					"<Separator>"			      },
  { "/" N_("Kingdom") "/" N_("Revolution"),	NULL,	kingdom_menu_callback,
    MENU_KINGDOM_REVOLUTION						      },

  { "/" N_("_View"),			NULL,		NULL,
    0,					"<Branch>"			      },
  { "/" N_("View") "/tearoff1",		NULL,		NULL,
    0,					"<Tearoff>"			      },
  { "/" N_("View") "/" N_("Map Grid"),		"<control>g",	view_menu_callback,
    MENU_VIEW_SHOW_MAP_GRID,		"<CheckItem>"			      },
  { "/" N_("View") "/" N_("Center View"),	"c",		view_menu_callback,
    MENU_VIEW_CENTER_VIEW						      },

  { "/" N_("_Orders")	,		NULL,		NULL,
    0,					"<Branch>"			      },
  { "/" N_("Orders") "/tearoff1",		NULL,		NULL,
    0,					"<Tearoff>"			      },
  { "/" N_("Orders") "/" N_("Build City"),	"b",		orders_menu_callback,
    MENU_ORDER_BUILD_CITY						      },
  { "/" N_("Orders") "/" N_("Build Road"),	"r",		orders_menu_callback,
    MENU_ORDER_ROAD							      },
  { "/" N_("Orders") "/" N_("Build Irrigation"), "i",		orders_menu_callback,
    MENU_ORDER_IRRIGATE							      },
  { "/" N_("Orders") "/" N_("Build Mine"),	"m",		orders_menu_callback,
    MENU_ORDER_MINE							      },
  { "/" N_("Orders") "/" N_("Transform Terrain"), "o",	orders_menu_callback,
    MENU_ORDER_TRANSFORM							      },
  { "/" N_("Orders") "/" N_("Build Fortress"), "<shift>f",	orders_menu_callback,
    MENU_ORDER_FORTRESS							      },
  { "/" N_("Orders") "/" N_("Build Airbase"), "e",		orders_menu_callback,
    MENU_ORDER_AIRBASE								      },
  { "/" N_("Orders") "/" N_("Clean Pollution"), "p",		orders_menu_callback,
    MENU_ORDER_POLLUTION						      },
  { "/" N_("Orders") "/sep1",		NULL,		NULL,
    0,					"<Separator>"			      },
  { "/" N_("Orders") "/" N_("Fortify"),	"f",		orders_menu_callback,
    MENU_ORDER_FORTIFY							      },
  { "/" N_("Orders") "/" N_("Sentry"),	"s",		orders_menu_callback,
    MENU_ORDER_SENTRY							      },
  { "/" N_("Orders") "/" N_("Pillage"),	"<shift>p",	orders_menu_callback,
    MENU_ORDER_PILLAGE							      },
  { "/" N_("Orders") "/sep2",		NULL,		NULL,
    0,					"<Separator>"			      },
  { "/" N_("Orders") "/" N_("Make Homecity"),	"h",		orders_menu_callback,
    MENU_ORDER_HOMECITY							      },
  { "/" N_("Orders") "/" N_("Unload"),	"u",		orders_menu_callback,
    MENU_ORDER_UNLOAD							      },
  { "/" N_("Orders") "/" N_("Wake up others"), "<shift>w",	orders_menu_callback,
    MENU_ORDER_WAKEUP_OTHERS						      },
  { "/" N_("Orders") "/sep3",		NULL,		NULL,
    0,					"<Separator>"			      },
  { "/" N_("Orders") "/" N_("Auto Settler"),	"a",		orders_menu_callback,
    MENU_ORDER_AUTO_SETTLER						      },
  { "/" N_("Orders") "/" N_("Auto Attack"),	"<shift>a",	orders_menu_callback,
    MENU_ORDER_AUTO_ATTACK						      },
  { "/" N_("Orders") "/" N_("Auto Explore"),	"x",		orders_menu_callback,
    MENU_ORDER_AUTO_EXPLORE						      },
  { "/" N_("Orders") "/" N_("Connect"),	        "<shift>c",	orders_menu_callback,
    MENU_ORDER_CONNECT							      },
  { "/" N_("Orders") "/" N_("Go to"),		"g",		orders_menu_callback,
    MENU_ORDER_GOTO							      },
  { "/" N_("Orders") "/" N_("Go|Airlift to City"), "l",	orders_menu_callback,
    MENU_ORDER_GOTO_CITY						      },
  { "/" N_("Orders") "/sep4",		NULL,		NULL,
    0,					"<Separator>"			      },
  { "/" N_("Orders") "/" N_("Disband Unit"),	"<shift>d",	orders_menu_callback,
    MENU_ORDER_DISBAND							      },
  { "/" N_("Orders") "/" N_("Help Build Wonder"), "<shift>b",	orders_menu_callback,
    MENU_ORDER_BUILD_WONDER						      },
  { "/" N_("Orders") "/" N_("Make Trade Route"), "<shift>r",	orders_menu_callback,
    MENU_ORDER_TRADEROUTE						      },
  { "/" N_("Orders") "/" N_("Explode Nuclear"), "<shift>n",	orders_menu_callback,
    MENU_ORDER_NUKE							      },
  { "/" N_("Orders") "/sep5",		NULL,		NULL,
    0,					"<Separator>"			      },
  { "/" N_("Orders") "/" N_("Wait"),		"w",		orders_menu_callback,
    MENU_ORDER_WAIT							      },
  { "/" N_("Orders") "/" N_("Done"),		"space",	orders_menu_callback,
    MENU_ORDER_DONE							      },

  { "/" N_("_Reports"),			NULL,		NULL,
    0,					"<Branch>"			      },
  { "/" N_("Reports") "/tearoff1",		NULL,		NULL,
    0,					"<Tearoff>"			      },
  { "/" N_("Reports") "/" N_("City Report"),	"F1",		reports_menu_callback,
    MENU_REPORT_CITY							      },
  { "/" N_("Reports") "/" N_("Military Report"), "F2",	reports_menu_callback,
    MENU_REPORT_MILITARY						      },
  { "/" N_("Reports") "/" N_("Trade Report"),	  "F5",		reports_menu_callback,
    MENU_REPORT_TRADE							      },
  { "/" N_("Reports") "/" N_("Science Report"), "F6",		reports_menu_callback,
    MENU_REPORT_SCIENCE							      },
  { "/" N_("Reports") "/sep1",		NULL,		NULL,
    0,					"<Separator>"			      },
  { "/" N_("Reports") "/" N_("Wonders of the World"),	"F7",	reports_menu_callback,
    MENU_REPORT_WOW							      },
  { "/" N_("Reports") "/" N_("Top Five Cities"),"F8",		reports_menu_callback,
    MENU_REPORT_TOP_CITIES						      },
  { "/" N_("Reports") "/" N_("Demographics"),	"F11",		reports_menu_callback,
    MENU_REPORT_DEMOGRAPHIC						      },
  { "/" N_("Reports") "/" N_("Spaceship"),	"F12",		reports_menu_callback,
    MENU_REPORT_SPACESHIP							      },

  { "/" N_("_Help"),			NULL,		NULL,
    0,					"<LastBranch>"			      },
  { "/" N_("Help") "/tearoff1",		NULL,		NULL,
    0,					"<Tearoff>"			      },
  { "/" N_("Help") "/" N_("Help Languages"),	NULL,		help_menu_callback,
    MENU_HELP_LANGUAGES							      },
  { "/" N_("Help") "/" N_("Help Connecting"),	NULL,		help_menu_callback,
    MENU_HELP_CONNECTING						      },
  { "/" N_("Help") "/" N_("Help Controls"),	NULL,		help_menu_callback,
    MENU_HELP_CONTROLS							      },
  { "/" N_("Help") "/" N_("Help Chatline"),	NULL,		help_menu_callback,
    MENU_HELP_CHATLINE							      },
  { "/" N_("Help") "/" N_("Help Playing"),	NULL,		help_menu_callback,
    MENU_HELP_PLAYING							      },
  { "/" N_("Help") "/sep1",		NULL,		NULL,
    0,					"<Separator>"			      },
  { "/" N_("Help") "/" N_("Help Improvements"), NULL,		help_menu_callback,
    MENU_HELP_IMPROVEMENTS						      },
  { "/" N_("Help") "/" N_("Help Units"),	NULL,		help_menu_callback,
    MENU_HELP_UNITS							      },
  { "/" N_("Help") "/" N_("Help Combat"),	NULL,		help_menu_callback,
    MENU_HELP_COMBAT							      },
  { "/" N_("Help") "/" N_("Help ZOC"),	NULL,		help_menu_callback,
    MENU_HELP_ZOC							      },
  { "/" N_("Help") "/" N_("Help Technology"),	NULL,		help_menu_callback,
    MENU_HELP_TECH							      },
  { "/" N_("Help") "/" N_("Help Terrain"),	NULL,		help_menu_callback,
    MENU_HELP_TERRAIN							      },
  { "/" N_("Help") "/" N_("Help Wonders"),	NULL,		help_menu_callback,
    MENU_HELP_WONDERS							      },
  { "/" N_("Help") "/" N_("Help Government"),	NULL,		help_menu_callback,
    MENU_HELP_GOVERNMENT						      },
  { "/" N_("Help") "/" N_("Help Happiness"),	NULL,		help_menu_callback,
    MENU_HELP_HAPPINESS							      },
  { "/" N_("Help") "/" N_("Help Space Race"),	NULL,		help_menu_callback,
    MENU_HELP_SPACE_RACE						      },
  { "/" N_("Help") "/sep2",		NULL,		NULL,
    0,					"<Separator>"			      },
  { "/" N_("Help") "/" N_("Copying"),		NULL,		help_menu_callback,
    MENU_HELP_COPYING							      },
  { "/" N_("Help") "/" N_("About"),		NULL,		help_menu_callback,
    MENU_HELP_ABOUT							      }
};


/****************************************************************
  gettext-translates each "/" delimited component of menu path,
  puts them back together, and returns as a static string.
  Any component which is of form "<foo>" is _not_ translated.
*****************************************************************/
static const char *translate_menu_path(const char *path)
{
#ifndef ENABLE_NLS
  return path;
#else
  static struct astring in, out, tmp;   /* these are never free'd */
  char *tok, *s, *trn, *t;
  int len;

  /* copy to in so can modify with strtok: */
  astr_minsize(&in, strlen(path)+1);
  strcpy(in.str, path);
  astr_minsize(&out, 1);
  out.str[0] = '\0';
  freelog(LOG_DEBUG, "trans: %s", in.str);

  s = in.str;
  while ((tok=strtok(s, "/")) != NULL) {
    len = strlen(tok);
    freelog(LOG_DEBUG, "tok \"%s\", len %d", tok, len);
    if (len && tok[0] == '<' && tok[len-1] == '>') {
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
    s = NULL;
  }
  return out.str;
#endif
}

/****************************************************************
...
*****************************************************************/
void setup_menus(GtkWidget *window, GtkWidget **menubar)
{
  const int nmenu_items = sizeof(menu_items)/sizeof(menu_items[0]);
  GtkAccelGroup *accel;
  int i;

  accel=gtk_accel_group_new();

  item_factory=gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<main>", accel);
   
  for(i=0; i<nmenu_items; i++) {
    menu_items[i].path = mystrdup(translate_menu_path(menu_items[i].path));
  }
  
  gtk_item_factory_create_items(item_factory, nmenu_items, menu_items, NULL);

  gtk_accel_group_attach(accel, GTK_OBJECT(window));

  if(menubar)
    *menubar=gtk_item_factory_get_widget(item_factory, "<main>");
}

/****************************************************************
...
*****************************************************************/
static void menus_set_sensitive(const char *path, int sensitive)
{
  GtkWidget *item;

  path = translate_menu_path(path);
  
  if(!(item=gtk_item_factory_get_widget(item_factory, path))) {
    freelog(LOG_VERBOSE,
	    "Can't set sensitivity for non-existent menu %s.", path);
    return;
  }

  if(GTK_IS_MENU(item))
    item=gtk_menu_get_attach_widget(GTK_MENU(item));
  gtk_widget_set_sensitive(item, sensitive);
}

#ifdef UNUSED 
/****************************************************************
...
*****************************************************************/
static void menus_set_shown(const char *path, int shown)
{
  GtkWidget *item;
  
  path = translate_menu_path(path);
  
  if(!(item=gtk_item_factory_get_widget(item_factory, path))) {
    freelog(LOG_VERBOSE, "Can't show non-existent menu %s.", path);
    return;
  }

  if(GTK_IS_MENU(item))
    item=gtk_menu_get_attach_widget(GTK_MENU(item));

  if(shown)
    gtk_widget_show(item);
  else
    gtk_widget_hide(item);
}
#endif /* UNUSED */

/****************************************************************
...
*****************************************************************/
static void menus_rename(const char *path, char *s)
{
  GtkWidget *item;
  
  path = translate_menu_path(path);
  
  if(!(item=gtk_item_factory_get_widget(item_factory, path))) {
    freelog(LOG_VERBOSE, "Can't rename non-existent menu %s.", path);
    return;
  }

  if (GTK_IS_MENU(item))
    item=gtk_menu_get_attach_widget(GTK_MENU(item));
  
  gtk_set_label(GTK_BIN(item)->child, s);
/*  gtk_accel_label_refetch(GTK_ACCEL_LABEL(GTK_BIN(item)->child));*/
}


/****************************************************************
...
*****************************************************************/
void update_menus(void)
{
  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE) {
    menus_set_sensitive("<main>/Reports", FALSE);
    menus_set_sensitive("<main>/Kingdom", FALSE);
    menus_set_sensitive("<main>/View", FALSE);
    menus_set_sensitive("<main>/Orders", FALSE);

    menus_set_sensitive("<main>/Game/Local Options", FALSE);
    menus_set_sensitive("<main>/Game/Message Options", FALSE);
    menus_set_sensitive("<main>/Game/Save Settings", FALSE);
    menus_set_sensitive("<main>/Game/Players", FALSE);
    menus_set_sensitive("<main>/Game/Messages", FALSE);
    menus_set_sensitive("<main>/Game/Server Opt initial", TRUE);
    menus_set_sensitive("<main>/Game/Server Opt ongoing", TRUE);
    menus_set_sensitive("<main>/Game/Export Log", TRUE);
    menus_set_sensitive("<main>/Game/Clear Log", TRUE);
    menus_set_sensitive("<main>/Game/Disconnect", TRUE);

  }
  else {
    struct unit *punit;
    menus_set_sensitive("<main>/Reports", TRUE);
    menus_set_sensitive("<main>/Kingdom", TRUE);
    menus_set_sensitive("<main>/View", TRUE);
    menus_set_sensitive("<main>/Orders", TRUE);
  
    menus_set_sensitive("<main>/Game/Local Options", TRUE);
    menus_set_sensitive("<main>/Game/Message Options", TRUE);
    menus_set_sensitive("<main>/Game/Save Settings", TRUE);
    menus_set_sensitive("<main>/Game/Players", TRUE);
    menus_set_sensitive("<main>/Game/Messages", TRUE);
    menus_set_sensitive("<main>/Game/Server Opt initial", TRUE);
    menus_set_sensitive("<main>/Game/Server Opt ongoing", TRUE);
    menus_set_sensitive("<main>/Game/Export Log", TRUE);
    menus_set_sensitive("<main>/Game/Clear Log", TRUE);
    menus_set_sensitive("<main>/Game/Disconnect", TRUE);

    menus_set_sensitive("<main>/Reports/Spaceship",
			(game.player_ptr->spaceship.state!=SSHIP_NONE));

    if((punit=get_unit_in_focus())) {
      char *chgfmt = _("Change to %s");
      char *transfmt = _("Transform to %s");
      char irrtext[128], mintext[128], transtext[128];
      char *roadtext;
      enum tile_terrain_type  ttype;
      struct tile_type *      tinfo;

      sz_strlcpy(irrtext, _("Build Irrigation"));
      sz_strlcpy(mintext, _("Build Mine"));
      sz_strlcpy(transtext, _("Transform Terrain"));
      
      menus_set_sensitive("<main>/Orders/Auto Settler",
			  (can_unit_do_auto(punit)
			   && unit_flag(punit->type, F_SETTLERS)));

      menus_set_sensitive("<main>/Orders/Auto Attack",
			  (can_unit_do_auto(punit)
			   && !unit_flag(punit->type, F_SETTLERS)));

      menus_set_sensitive("<main>/Orders/Build City",
			   (can_unit_build_city(punit) ||
			    can_unit_add_to_city(punit)));
      menus_set_sensitive("<main>/Orders/Build Fortress",
			   can_unit_do_activity(punit, ACTIVITY_FORTRESS));
      menus_set_sensitive("<main>/Orders/Build Airbase",
			   can_unit_do_activity(punit, ACTIVITY_AIRBASE));
      menus_set_sensitive("<main>/Orders/Build Road",
			   can_unit_do_activity(punit, ACTIVITY_ROAD) ||
			   can_unit_do_activity(punit, ACTIVITY_RAILROAD));
      menus_set_sensitive("<main>/Orders/Connect",
			  can_unit_do_connect(punit, ACTIVITY_IDLE)
			  && has_capability ("unit_connect", aconnection.capability));
      menus_set_sensitive("<main>/Orders/Clean Pollution",
			   can_unit_do_activity(punit, ACTIVITY_POLLUTION) ||
			   can_unit_paradropped(punit));
      menus_set_sensitive("<main>/Orders/Fortify",
			   can_unit_do_activity(punit, ACTIVITY_FORTIFY));
      menus_set_sensitive("<main>/Orders/Sentry",
			   can_unit_do_activity(punit, ACTIVITY_SENTRY));
      menus_set_sensitive("<main>/Orders/Pillage",
			   can_unit_do_activity(punit, ACTIVITY_PILLAGE));
      menus_set_sensitive("<main>/Orders/Auto Explore",
			   can_unit_do_activity(punit, ACTIVITY_EXPLORE));
      menus_set_sensitive("<main>/Orders/Build Mine",
			   can_unit_do_activity(punit, ACTIVITY_MINE));
      menus_set_sensitive("<main>/Orders/Build Irrigation",
			   can_unit_do_activity(punit, ACTIVITY_IRRIGATE));
      menus_set_sensitive("<main>/Orders/Transform Terrain",
			   can_unit_do_activity(punit, ACTIVITY_TRANSFORM));
      menus_set_sensitive("<main>/Orders/Make Homecity",
			   can_unit_change_homecity(punit));
      menus_set_sensitive("<main>/Orders/Explode Nuclear",
                           unit_flag(punit->type, F_NUCLEAR));
      menus_set_sensitive("<main>/Orders/Unload",
			   get_transporter_capacity(punit)>0);
      menus_set_sensitive("<main>/Orders/Wake up others", 
			   is_unit_activity_on_tile(ACTIVITY_SENTRY,
				punit->x, punit->y));
      menus_set_sensitive("<main>/Orders/Help Build Wonder",
			   unit_can_help_build_wonder_here(punit));
      menus_set_sensitive("<main>/Orders/Make Trade Route",
			   unit_can_est_traderoute_here(punit));

      if (unit_flag(punit->type, F_CITIES)
	 && map_get_city(punit->x, punit->y)) {
       menus_rename("<main>/Orders/Build City", _("Add to City"));
      } else {
       menus_rename("<main>/Orders/Build City", _("Build City"));
      }

      ttype = map_get_tile(punit->x, punit->y)->terrain;
      tinfo = get_tile_type(ttype);
      if ((tinfo->irrigation_result != T_LAST) && (tinfo->irrigation_result != ttype))
	{
	  my_snprintf (irrtext, sizeof(irrtext), chgfmt,
		   (get_tile_type(tinfo->irrigation_result))->terrain_name);
	}
      else if ((map_get_tile(punit->x,punit->y)->special&S_IRRIGATION) &&
	       improvement_exists(B_SUPERMARKET))
	{
	  sz_strlcpy (irrtext, _("Build Farmland"));
	}
      if ((tinfo->mining_result != T_LAST) && (tinfo->mining_result != ttype))
	{
	  my_snprintf (mintext, sizeof(mintext), chgfmt,
		   (get_tile_type(tinfo->mining_result))->terrain_name);
	}
      if ((tinfo->transform_result != T_LAST) && (tinfo->transform_result != ttype))
	{
	  my_snprintf (transtext, sizeof(transtext), transfmt,
		   (get_tile_type(tinfo->transform_result))->terrain_name);
	}

      if (unit_flag(punit->type, F_PARATROOPERS)) {
       menus_rename("<main>/Orders/Clean Pollution", _("Paradrop"));
      } else {
       menus_rename("<main>/Orders/Clean Pollution", _("Clean Pollution"));
      }

      menus_rename("<main>/Orders/Build Irrigation", irrtext);
      menus_rename("<main>/Orders/Build Mine", mintext);
      menus_rename("<main>/Orders/Transform Terrain", transtext);
    
      if (map_get_tile(punit->x,punit->y)->special&S_ROAD) {
	roadtext = _("Build Railroad");
	road_activity=ACTIVITY_RAILROAD;  
      } else {
	roadtext = _("Build Road");
	road_activity=ACTIVITY_ROAD;  
      }
      menus_rename("<main>/Orders/Build Road", roadtext);

      menus_set_sensitive("<main>/Orders", TRUE);
    }
    else
      menus_set_sensitive("<main>/Orders", FALSE);
  }
}
