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
#ifndef FC__GUI_MAIN_H
#define FC__GUI_MAIN_H

#include "gui_main_g.h"

#define COPYRIGHTSTRING "©1999-2000 by Sebastian Bauer"
#define AUTHORSTRING    "Sebastian Bauer"
#ifdef __SASC
#define VERSIONSTRING   "$VER: civclient 1.13 " __AMIGADATE__ " " VERSION_STRING
#else
#define VERSIONSTRING   "$VER: civclient 1.13 (" __DATE__ ") " VERSION_STRING
#endif

enum{

  MENU_GAME=1,
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

  MENU_KINGDOM,
  MENU_KINGDOM_TAX_RATE,
  MENU_KINGDOM_FIND_CITY,
  MENU_KINGDOM_WORKLISTS,
  MENU_KINGDOM_REVOLUTION,

  MENU_VIEW,
  MENU_VIEW_SHOW_MAP_GRID,
  MENU_VIEW_SHOW_CITY_NAMES,
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
  MENU_VIEW_CENTER_VIEW,

  MENU_ORDER,
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
  MENU_ORDER_PATROL,
  MENU_ORDER_POLLUTION,
  MENU_ORDER_FALLOUT,
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
  MENU_ORDER_DIPLOMAT_DLG,
  MENU_ORDER_DONE,
  MENU_ORDER_NUKE,

  MENU_REPORT,
  MENU_REPORT_CITY,
  MENU_REPORT_SCIENCE,
  MENU_REPORT_TRADE,
  MENU_REPORT_MILITARY,
  MENU_REPORT_DEMOGRAPHIC,
  MENU_REPORT_TOP_CITIES,
  MENU_REPORT_WOW,
  MENU_REPORT_SPACESHIP,

  MENU_HELP,
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
  MENU_HELP_LANGUAGES,

  UNIT_NORTH,
  UNIT_SOUTH,
  UNIT_EAST,
  UNIT_WEST,
  UNIT_NORTH_EAST,
  UNIT_NORTH_WEST,
  UNIT_SOUTH_EAST,
  UNIT_SOUTH_WEST,
  UNIT_ESCAPE,
  UNIT_POPUP_CITY,
  UNIT_POPUP_UNITLIST,

  UNIT_ACTIVATE,
  UNIT_GOTOLOC,
  UNIT_CONNECT_TO,

  KEMAP_GRID_TOGGLE,

  NEXT_UNIT,
  END_TURN,

  CITY_POPUP,

  UNIT_UPGRADE
};

void do_unit_function( struct unit *punit, ULONG value);

extern Object *app;
extern Object *main_people_text;
extern Object *main_year_text;
extern Object *main_gold_text;
extern Object *main_tax_text;
extern Object *main_bulb_sprite;
extern Object *main_sun_sprite;
extern Object *main_flake_sprite;
extern Object *main_government_sprite;
extern Object *main_timeout_text;
extern Object *main_econ_sprite[10];
extern Object *main_overview_area;
extern Object *main_overview_group;
extern Object *main_unitname_text;
extern Object *main_moves_text;
extern Object *main_terrain_text;
extern Object *main_hometown_text;
extern Object *main_unit_unit;
extern Object *main_below_group;
extern Object *main_map_area;
extern Object *main_turndone_button;
extern Object *main_output_listview;
extern Object *main_wnd;

#endif  /* FC__GUI_MAIN_H */
