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
#include <windows.h>
#include <ctype.h>
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
#include "gui_main.h"
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
#include "tilespec.h"
#include "wldlg.h"   

#include "menu.h"
enum MenuID {
  IDM_GAME_MENU=32,
  IDM_GAME_LOCAL_OPT,
  IDM_GAME_MESSAGE_OPT,
  IDM_GAME_SAVE_SETTINGS,
  IDM_GAME_PLAYERS,
  IDM_GAME_MESSAGES,
  IDM_GAME_SERVER_INIT,
  IDM_GAME_SERVER_OPT_ON,
  IDM_GAME_EXPORT, 
  IDM_GAME_CLEAR,
  IDM_GAME_DISCONNECT,
  IDM_GAME_QUIT,
  
  IDM_KINGDOM_MENU,
  IDM_KINGDOM_TAX,
  IDM_KINGDOM_CITY,
  IDM_KINGDOM_WORK,
  IDM_KINGDOM_REVOLUTION,

  IDM_VIEW_MENU,
  IDM_VIEW_GRID, 
  IDM_VIEW_NAMES,
  IDM_VIEW_PROD,
  IDM_VIEW_CENTER,
  IDM_VIEW_TERRAIN,
  IDM_VIEW_COASTLINE,
  IDM_VIEW_SPECIALS,
  IDM_VIEW_POLLUTION,
  IDM_VIEW_CITIES,
  IDM_VIEW_UNITS,
  IDM_VIEW_FOCUS_UNIT,
  IDM_VIEW_FOG_OF_WAR,

  IDM_ORDERS_MENU,
  IDM_ORDERS_BUILDCITY,
  IDM_ORDERS_ROAD,
  IDM_ORDERS_IRRIGATION,
  IDM_ORDERS_FOREST,
  IDM_ORDERS_TRANSFORM,
  IDM_ORDERS_FORTRESS,
  IDM_ORDERS_AIRBASE, 
  IDM_ORDERS_POLLUTION,
  IDM_ORDERS_FALLOUT,

  IDM_ORDERS_FORTIFY, 
  IDM_ORDERS_SENTRY,
  IDM_ORDERS_PILLAGE,

  IDM_ORDERS_HOME,
  IDM_ORDERS_UNLOAD,
  IDM_ORDERS_WAKEUP,

  IDM_ORDERS_AUTOSETTLER,
  IDM_ORDERS_AUTOATTACK,
  IDM_ORDERS_AUTOEXPLORE,
  IDM_ORDERS_CONNECT,
  IDM_ORDERS_GOTO,
  IDM_ORDERS_AIRLIFT,

  IDM_ORDERS_DISBAND,
  IDM_ORDERS_WONDER,
  IDM_ORDERS_TROUTE,
  IDM_ORDERS_EXPLODE,

  IDM_ORDERS_WAIT,
  IDM_ORDERS_DONE,

  IDM_REPORTS_MENU,
  IDM_REPORTS_CITY,
  IDM_REPORTS_MILITARY,
  IDM_REPORTS_TRADE,
  IDM_REPORTS_SCIENCE,

  IDM_REPORTS_WONDERS,
  IDM_REPORTS_CITIES,
  IDM_REPORTS_DEMOGRAPHICS,
  IDM_REPORTS_SPACESHIP,

  IDM_HELP_MENU,
  IDM_HELP_LANGUAGES,
  IDM_HELP_CONNECTING,
  IDM_HELP_CHATLINE,
  IDM_HELP_CONTROLS,
  IDM_HELP_PLAYING,
  IDM_HELP_IMPROVEMENTS,
  IDM_HELP_UNITS,
  IDM_HELP_COMBAT,
  IDM_HELP_ZOC,
  IDM_HELP_TECH,
  IDM_HELP_TERRAIN,
  IDM_HELP_WONDERS,
  IDM_HELP_GOVERNMENT,
  IDM_HELP_HAPPINESS,
  IDM_HELP_SPACE_RACE,
  IDM_HELP_COPYING,
  IDM_HELP_ABOUT,

  IDM_NUMPAD_BASE,
  IDM_NUMPAD1,
  IDM_NUMPAD2,
  IDM_NUMPAD3,
  IDM_NUMPAD4,
  IDM_NUMPAD5,
  IDM_NUMPAD6,
  IDM_NUMPAD7,
  IDM_NUMPAD8,
  IDM_NUMPAD9
};

#define MAX_ACCEL 512
static ACCEL numpadaccel[]={
  { FVIRTKEY,VK_NUMPAD1,IDM_NUMPAD1},
  { FVIRTKEY,VK_END,IDM_NUMPAD1},
  { FVIRTKEY,VK_NUMPAD2,IDM_NUMPAD2},
  { FVIRTKEY,VK_DOWN,IDM_NUMPAD2},
  { FVIRTKEY,VK_NUMPAD3,IDM_NUMPAD3},
  { FVIRTKEY,VK_NEXT,IDM_NUMPAD3},
  { FVIRTKEY,VK_NUMPAD4,IDM_NUMPAD4},
  { FVIRTKEY,VK_LEFT,IDM_NUMPAD4},
  { FVIRTKEY,VK_NUMPAD5,IDM_NUMPAD5},
  { FVIRTKEY,VK_NUMPAD6,IDM_NUMPAD6},
  { FVIRTKEY,VK_RIGHT,IDM_NUMPAD6},
  { FVIRTKEY,VK_NUMPAD7,IDM_NUMPAD7},
  { FVIRTKEY,VK_HOME,IDM_NUMPAD7},
  { FVIRTKEY,VK_NUMPAD8,IDM_NUMPAD8},
  { FVIRTKEY,VK_UP,IDM_NUMPAD8},
  { FVIRTKEY,VK_NUMPAD9,IDM_NUMPAD9},
  { FVIRTKEY,VK_PRIOR,IDM_NUMPAD9},
  { FVIRTKEY,VK_RETURN,ID_TURNDONE}};
static ACCEL menuaccel[MAX_ACCEL];
static int accelcount;
enum unit_activity road_activity;   

struct my_menu {
  char *name;
  int id;
};

/**************************************************************************

**************************************************************************/
HACCEL my_create_menu_acceltable()
{
  return CreateAcceleratorTable(menuaccel,accelcount);
}
/**************************************************************************

**************************************************************************/
static void my_add_menu_accelerator(char *item,int cmd)
{
  char *plus;
  char *tab;
  ACCEL newaccel;
   
  if (!accelcount) {
    
    for(;accelcount<sizeof(numpadaccel)/sizeof(ACCEL);accelcount++)
      menuaccel[accelcount]=numpadaccel[accelcount];
  }
 
  tab=strchr(item,'\t');
  if(!tab)
    return;
  plus=strchr(item,'+');
  if (!plus)
    plus=tab;
  plus++;
  tab++;
  /* fkeys */
  if ((*plus=='F')&&(isdigit(plus[1]))) {
    if (isdigit(plus[2]))
      newaccel.key=VK_F10+(plus[2]-'0');
    else
      newaccel.key=VK_F1+(plus[1]-'1');
    newaccel.fVirt=FVIRTKEY;
  } else if (*plus) { /* standard ascii */
    newaccel.key=toupper(*plus);
    newaccel.fVirt=FVIRTKEY;
  } else {
    return;
  }
  
  /* Modifiers (Alt,Shift,Ctl) */
  if (mystrncasecmp(tab,"Shift",5)==0)
    newaccel.fVirt|=FSHIFT;
  else if (mystrncasecmp(tab,"Ctl",3)==0)
    newaccel.fVirt|=FCONTROL;
  else if (mystrncasecmp(tab,"Alt",3)==0)
    newaccel.fVirt|=FALT;
  printf("Tab: %s: key: %x fVirt: %x\n",tab,newaccel.key,newaccel.fVirt);
  newaccel.cmd=cmd;
  if (accelcount<MAX_ACCEL) {
    menuaccel[accelcount]=newaccel;
    accelcount++;
  }
}

/**************************************************************************

**************************************************************************/
static void my_enable_menu(HMENU hmenu,int id,int state)
{
  if (state)
    EnableMenuItem(hmenu,id,MF_BYCOMMAND | MF_ENABLED);
  else
    EnableMenuItem(hmenu,id,MF_BYCOMMAND | MF_GRAYED);
}


static struct my_menu game_menu[16]={
  {"Local Options",IDM_GAME_LOCAL_OPT},
  {"Message Options",IDM_GAME_MESSAGE_OPT},
  {"Save Settings",IDM_GAME_SAVE_SETTINGS},
  {"",-1},
  {"Players\tF3",IDM_GAME_PLAYERS},
  {"Messages\tF10",IDM_GAME_MESSAGES},
  {"",-1},
  {"Server Opt initial",IDM_GAME_SERVER_INIT},
  {"Server Opt ongoing",IDM_GAME_SERVER_OPT_ON},
  {"",-1},
  {"Export Log",IDM_GAME_EXPORT},
  {"Clear Log",IDM_GAME_CLEAR},
  {"",-1},
  {"Disconnect",IDM_GAME_DISCONNECT},
  {"Quit\tCtl+Q",IDM_GAME_QUIT},
  {NULL,0}
};
  
static struct my_menu kingdom_menu[7]={
  {"Tax Rates\tShift+T",IDM_KINGDOM_TAX},
  { "",-1},
  { "Find City\tCtl+F",IDM_KINGDOM_CITY},
  { "Worklists\tShift+L",IDM_KINGDOM_WORK},
  { "",-1},
  { "Revolution",IDM_KINGDOM_REVOLUTION}, 
  {NULL,0}
};

static struct my_menu view_menu[]={
  {"Map Grid\tCtl+G",IDM_VIEW_GRID},
  {"City Names\tCtl+N",IDM_VIEW_NAMES},
  {"City Productions\tCtl+P",IDM_VIEW_PROD},
  {"Terrain",IDM_VIEW_TERRAIN},
  {"Coastline",IDM_VIEW_COASTLINE},
  {"Specials",IDM_VIEW_SPECIALS},
  {"Pollution && Fallout",IDM_VIEW_POLLUTION},
  {"Cities",IDM_VIEW_CITIES},
  {"Units",IDM_VIEW_UNITS},
  {"Focus Unit",IDM_VIEW_FOCUS_UNIT},
  {"Fog of War",IDM_VIEW_FOG_OF_WAR},
  {"",-1},
  {"Center View\tC",IDM_VIEW_CENTER},
  {NULL,0}
};

static struct my_menu orders_menu[]={
  { "Build City\tB",IDM_ORDERS_BUILDCITY},
  { "Build Road\tR",IDM_ORDERS_ROAD},
  { "Build Irrigation\tI",IDM_ORDERS_IRRIGATION},
  { "Change to Forest\tM",IDM_ORDERS_FOREST},
  { "Transform to Hills\tO",IDM_ORDERS_TRANSFORM},
  { "Build Fortress\tshift+F",IDM_ORDERS_FORTRESS},
  { "Build Airbase\tE",IDM_ORDERS_AIRBASE},
  { "Clean Pollution\tP",IDM_ORDERS_POLLUTION},
  { "Clean Nuclear Fallout\tN",IDM_ORDERS_FALLOUT},
  { "",-1},
  { "Fortify\tF",IDM_ORDERS_FORTIFY},
  { "Sentry\tS",IDM_ORDERS_SENTRY},
  { "Pillage\tShift+P",IDM_ORDERS_PILLAGE},
  { "",-1},
  { "Make Homecity\tH",IDM_ORDERS_HOME},
  { "Unload\tU",IDM_ORDERS_UNLOAD},
  { "Wake up others\tShift+W",IDM_ORDERS_WAKEUP},
  { "",-1},
  { "Auto Settler\tA",IDM_ORDERS_AUTOSETTLER},
  { "Auto Attack\tShift+A",IDM_ORDERS_AUTOATTACK},
  { "Auto Explore\tX",IDM_ORDERS_AUTOEXPLORE},
  { "Connect\tShift+C",IDM_ORDERS_CONNECT},
  { "Go to\tG",IDM_ORDERS_GOTO},
  { "Go/Airlift to City\tL",IDM_ORDERS_AIRLIFT},
  { "",-1},
  { "Disband Unit\tShift+D",IDM_ORDERS_DISBAND},
  { "Help Build Wonder\tShift+B",IDM_ORDERS_WONDER},
  { "Make Trade Route\tShift+R",IDM_ORDERS_TROUTE},
  { "Explode Nuclear\tShift+N",IDM_ORDERS_EXPLODE},
  { "",-1},
  { "Wait\tW",IDM_ORDERS_WAIT},
  { "Done\tSpace",IDM_ORDERS_DONE},
  {NULL,0}
};

struct my_menu reports_menu[10]={
  { "Cities\tF1",IDM_REPORTS_CITY},
  { "Units\tF2",IDM_REPORTS_MILITARY},
  { "Economy\tF5",IDM_REPORTS_TRADE},
  { "Science\tF6",IDM_REPORTS_SCIENCE},
  { "",-1},
  { "Wonders of the World\tF7",IDM_REPORTS_WONDERS},
  { "Top Five Cities\tF8",IDM_REPORTS_CITIES},
  { "Demographics\tF11",IDM_REPORTS_DEMOGRAPHICS},
  { "Spaceship\tF12",IDM_REPORTS_SPACESHIP},
  { NULL,0}
};

struct my_menu help_menu[]= {
  {"Languages",IDM_HELP_LANGUAGES},
  {"Connecting",IDM_HELP_CONNECTING},
  {"Controls",IDM_HELP_CONTROLS},
  {"Chatline",IDM_HELP_CHATLINE},
  {"Playing",IDM_HELP_PLAYING},
  {"",-1},
  {"Improvements",IDM_HELP_IMPROVEMENTS},
  {"Units",IDM_HELP_UNITS},
  {"Combat",IDM_HELP_COMBAT},
  {"ZOC",IDM_HELP_ZOC},
  {"Technology",IDM_HELP_TECH},
  {"Wonders",IDM_HELP_WONDERS},
  {"Government",IDM_HELP_GOVERNMENT},
  {"Happiness",IDM_HELP_HAPPINESS},
  {"Space Race",IDM_HELP_SPACE_RACE},
  {"",-1},
  {"Copying",IDM_HELP_COPYING},
  {"About",IDM_HELP_ABOUT},
  {NULL,0}};

/**************************************************************************

**************************************************************************/
static int my_append_menu(HMENU menu, struct my_menu *item)
{
  char menustr[256];
  char *tr;
  char translated[256];
  char *accel;
  sz_strlcpy(menustr,item->name);
  if ((accel=strchr(menustr,'\t')))
    {
      my_add_menu_accelerator(item->name,item->id);
      accel[0]=0;
      accel++;
    }
  tr=_(menustr);
  sz_strlcpy(translated,tr);
  if (accel)
    {
      sz_strlcat(translated,"\t");
      sz_strlcat(translated,accel);
    }
  return AppendMenu(menu,MF_STRING,item->id,translated);
}

/**************************************************************************

**************************************************************************/
HMENU my_create_menu(struct my_menu *items)
{
  HMENU menu;
  menu=CreatePopupMenu();
  while(items->name)
    {
      if (items->id==-1)
	{
	  AppendMenu(menu,MF_SEPARATOR,-1,NULL);
	}
      else
	{
	  my_append_menu(menu,items);
	}
      items++;
    }
  return menu;
}

/**************************************************************************

**************************************************************************/
void handle_numpad(int code)
{
  if (is_isometric) {
    switch (code)
      {
      case IDM_NUMPAD_BASE+1: key_move_south(); break;
      case IDM_NUMPAD_BASE+2: key_move_south_east(); break;
      case IDM_NUMPAD_BASE+3: key_move_east(); break;
      case IDM_NUMPAD_BASE+4: key_move_south_west(); break;
      case IDM_NUMPAD_BASE+5: focus_to_next_unit(); break;
      case IDM_NUMPAD_BASE+6: key_move_north_east(); break;
      case IDM_NUMPAD_BASE+7: key_move_west(); break;
      case IDM_NUMPAD_BASE+8: key_move_north_west(); break;
      case IDM_NUMPAD_BASE+9: key_move_north(); break;
      }
  } else {
    switch (code) 
      { 
      case IDM_NUMPAD_BASE+1: key_move_south_west(); break;
      case IDM_NUMPAD_BASE+2: key_move_south(); break;
      case IDM_NUMPAD_BASE+3: key_move_south_east(); break;
      case IDM_NUMPAD_BASE+4: key_move_west(); break;
      case IDM_NUMPAD_BASE+5: focus_to_next_unit(); break;
      case IDM_NUMPAD_BASE+6: key_move_east(); break;
      case IDM_NUMPAD_BASE+7: key_move_north_west(); break;
      case IDM_NUMPAD_BASE+8: key_move_north(); break;
      case IDM_NUMPAD_BASE+9: key_move_north_east(); break;
      }
  }
}

/**************************************************************************

**************************************************************************/
void handle_menu(int code)
{
  if ((code>IDM_NUMPAD_BASE)&&(code<IDM_NUMPAD_BASE+10)) {
    handle_numpad(code);
    return;
  }
  switch((enum MenuID)code)
    {
    case IDM_GAME_LOCAL_OPT:
      popup_option_dialog();
      break;
    case IDM_GAME_MESSAGE_OPT:
      popup_messageopt_dialog();
      break;
    case IDM_GAME_SAVE_SETTINGS:
      save_options();
      break;
    case IDM_GAME_PLAYERS:
      popup_players_dialog();
      break;
    case IDM_GAME_MESSAGES:
      popup_meswin_dialog();
      break;
    case IDM_GAME_SERVER_INIT:
       send_report_request(REPORT_SERVER_OPTIONS1);
       break;   
    case IDM_GAME_SERVER_OPT_ON:
      send_report_request(REPORT_SERVER_OPTIONS2);
      break;     
    case IDM_GAME_EXPORT:
      log_output_window();
      break;
    case IDM_GAME_CLEAR:
      clear_output_window();
      break;
    case IDM_GAME_DISCONNECT:
      disconnect_from_server();
      break;
    case IDM_GAME_QUIT:
      exit(0);
      break;
    case IDM_KINGDOM_TAX:
      popup_rates_dialog();
      break;
    case IDM_KINGDOM_CITY:
      popup_find_dialog();
      break;
    case IDM_KINGDOM_WORK:
      popup_worklists_dialog(game.player_ptr);
      break;
    case IDM_KINGDOM_REVOLUTION:
      popup_revolution_dialog();
      break;
    case IDM_VIEW_GRID:
      key_map_grid_toggle();
      CheckMenuItem(GetMenu(root_window),IDM_VIEW_GRID,MF_BYCOMMAND |
		    (draw_map_grid?MF_CHECKED:MF_UNCHECKED));
      break;
    case IDM_VIEW_NAMES:
      key_city_names_toggle();
      CheckMenuItem(GetMenu(root_window),IDM_VIEW_NAMES,MF_BYCOMMAND |
		    (draw_city_names?MF_CHECKED:MF_UNCHECKED));
      break;
    case IDM_VIEW_PROD:
      key_city_productions_toggle();
      CheckMenuItem(GetMenu(root_window),IDM_VIEW_PROD,MF_BYCOMMAND |
		    (draw_city_productions?MF_CHECKED:MF_UNCHECKED));
      break;
    case IDM_VIEW_TERRAIN:
      key_terrain_toggle();
      CheckMenuItem(GetMenu(root_window),IDM_VIEW_TERRAIN,MF_BYCOMMAND |
		    (draw_terrain?MF_CHECKED:MF_UNCHECKED));
      my_enable_menu(GetMenu(root_window),IDM_VIEW_COASTLINE,!draw_city_names);
      break;
    case IDM_VIEW_COASTLINE:
      key_coastline_toggle();
      CheckMenuItem(GetMenu(root_window),IDM_VIEW_COASTLINE,MF_BYCOMMAND |
		    (draw_coastline?MF_CHECKED:MF_UNCHECKED));
      break;
    case IDM_VIEW_SPECIALS:
      key_specials_toggle();
      CheckMenuItem(GetMenu(root_window),IDM_VIEW_SPECIALS,MF_BYCOMMAND |
		    (draw_specials?MF_CHECKED:MF_UNCHECKED));
      break;
    case IDM_VIEW_POLLUTION:
      key_pollution_toggle();
      CheckMenuItem(GetMenu(root_window),IDM_VIEW_POLLUTION,MF_BYCOMMAND |
		    (draw_pollution?MF_CHECKED:MF_UNCHECKED));
      break;
    case IDM_VIEW_CITIES:
      key_cities_toggle();
      CheckMenuItem(GetMenu(root_window),IDM_VIEW_CITIES,MF_BYCOMMAND |
		    (draw_cities?MF_CHECKED:MF_UNCHECKED));
      break;
    case IDM_VIEW_UNITS:
      key_units_toggle();
      CheckMenuItem(GetMenu(root_window),IDM_VIEW_UNITS,MF_BYCOMMAND |
		    (draw_units?MF_CHECKED:MF_UNCHECKED));
      my_enable_menu(GetMenu(root_window),IDM_VIEW_FOCUS_UNIT,
		     !draw_units); 
      break;
    case IDM_VIEW_FOCUS_UNIT:
      key_focus_unit_toggle();
      CheckMenuItem(GetMenu(root_window),IDM_VIEW_FOCUS_UNIT,MF_BYCOMMAND |
		    (draw_focus_unit?MF_CHECKED:MF_UNCHECKED));
      break;
    case IDM_VIEW_FOG_OF_WAR:
      key_fog_of_war_toggle();
      CheckMenuItem(GetMenu(root_window),IDM_VIEW_FOG_OF_WAR,MF_BYCOMMAND |
		    (draw_fog_of_war?MF_CHECKED:MF_UNCHECKED));
      break;
    case IDM_VIEW_CENTER:
      center_on_unit();
      break;
    case IDM_ORDERS_AUTOSETTLER:
    case IDM_ORDERS_AUTOATTACK:
      if(get_unit_in_focus())
	request_unit_auto(get_unit_in_focus());
      break;
    case IDM_ORDERS_BUILDCITY:
      if(get_unit_in_focus())
	request_unit_build_city(get_unit_in_focus());
      break;
    case IDM_ORDERS_IRRIGATION:
      if(get_unit_in_focus())
	request_new_unit_activity(get_unit_in_focus(), ACTIVITY_IRRIGATE);
      break;
    case IDM_ORDERS_FORTRESS:
      if(get_unit_in_focus())
	request_new_unit_activity(get_unit_in_focus(), ACTIVITY_FORTRESS);
      break;
    case IDM_ORDERS_AIRBASE:
      key_unit_airbase();
      break;
    case IDM_ORDERS_FOREST:
      if(get_unit_in_focus())
	request_new_unit_activity(get_unit_in_focus(), ACTIVITY_MINE);
      break;
    case IDM_ORDERS_TRANSFORM:
      if(get_unit_in_focus())
	request_new_unit_activity(get_unit_in_focus(), ACTIVITY_TRANSFORM);
      break;
    case IDM_ORDERS_ROAD:
      if(get_unit_in_focus())
	request_new_unit_activity(get_unit_in_focus(), road_activity);
      break;
    case IDM_ORDERS_CONNECT:
      if(get_unit_in_focus())
	request_unit_connect();
      break;
    case IDM_ORDERS_POLLUTION:
      if(can_unit_paradrop(get_unit_in_focus()))
	key_unit_paradrop();
      else
	key_unit_pollution();
      break;
    case IDM_ORDERS_FALLOUT:
      if(get_unit_in_focus())
	request_new_unit_activity(get_unit_in_focus(), ACTIVITY_FALLOUT);
      break;
    case IDM_ORDERS_HOME:
      if(get_unit_in_focus())
	request_unit_change_homecity(get_unit_in_focus());
      break;
    case IDM_ORDERS_FORTIFY:
      if(get_unit_in_focus())
	request_new_unit_activity(get_unit_in_focus(), ACTIVITY_FORTIFYING);
      break;
    case IDM_ORDERS_SENTRY:
      if(get_unit_in_focus())
	request_new_unit_activity(get_unit_in_focus(), ACTIVITY_SENTRY);
      break;
    case IDM_ORDERS_WAIT:
      if(get_unit_in_focus())
	request_unit_wait(get_unit_in_focus());
      break;
    case IDM_ORDERS_UNLOAD:
      if(get_unit_in_focus())
	request_unit_unload(get_unit_in_focus());
      break;
    case IDM_ORDERS_WAKEUP:
      if(get_unit_in_focus())
	request_unit_wakeup(get_unit_in_focus());   
      break;
    case IDM_ORDERS_GOTO:
      if(get_unit_in_focus())
	request_unit_goto();
      break;
    case IDM_ORDERS_AIRLIFT:
      if(get_unit_in_focus())
	popup_goto_dialog();
      break;
    case IDM_ORDERS_DISBAND:
      if(get_unit_in_focus())
	request_unit_disband(get_unit_in_focus());
     break;
    case IDM_ORDERS_PILLAGE:
      if(get_unit_in_focus())
	request_unit_pillage(get_unit_in_focus());
      break;
    case IDM_ORDERS_AUTOEXPLORE:
      if(get_unit_in_focus())
	request_new_unit_activity(get_unit_in_focus(), ACTIVITY_EXPLORE);
      break;
    case IDM_ORDERS_WONDER:
      if(get_unit_in_focus())
	request_unit_caravan_action(get_unit_in_focus(),
				    PACKET_UNIT_HELP_BUILD_WONDER);
      break;
    case IDM_ORDERS_TROUTE:
      if(get_unit_in_focus())
	request_unit_caravan_action(get_unit_in_focus(),
				    PACKET_UNIT_ESTABLISH_TRADE);
      break;
    case IDM_ORDERS_DONE:
      if(get_unit_in_focus())
	request_unit_move_done();
      break;
    case IDM_ORDERS_EXPLODE:
      if(get_unit_in_focus())
	request_unit_nuke(get_unit_in_focus());
      break;
    case IDM_REPORTS_CITY:
      popup_city_report_dialog(0);
      break;
    case IDM_REPORTS_MILITARY:
      popup_activeunits_report_dialog(0);
      break;
    case IDM_REPORTS_SCIENCE:
      popup_science_dialog(0);
      break;
    case IDM_REPORTS_DEMOGRAPHICS:
      send_report_request(REPORT_DEMOGRAPHIC);
      break;
    case IDM_REPORTS_TRADE:
      popup_economy_report_dialog(0);
      break;  
    case IDM_REPORTS_CITIES:
      send_report_request(REPORT_TOP_5_CITIES);
      break;       
    case IDM_REPORTS_WONDERS:
      send_report_request(REPORT_WONDERS_OF_THE_WORLD);
      break;          
    case IDM_REPORTS_SPACESHIP:
      popup_spaceship_dialog(game.player_ptr);
      break; 
    case ID_TURNDONE:
      EnableWindow(turndone_button,FALSE);
      user_ended_turn();
      break;
    case IDM_HELP_LANGUAGES:
      popup_help_dialog_string(HELP_LANGUAGES_ITEM);
      break;
    
    case IDM_HELP_CONNECTING:
      popup_help_dialog_string(HELP_CONNECTING_ITEM);
      break;
    case IDM_HELP_CHATLINE:
      popup_help_dialog_string(HELP_CHATLINE_ITEM);
      break;
    case IDM_HELP_CONTROLS:
      popup_help_dialog_string(HELP_CONTROLS_ITEM);
      break;
    case IDM_HELP_PLAYING:
      popup_help_dialog_string(HELP_PLAYING_ITEM);
      break;
    case IDM_HELP_IMPROVEMENTS:
      popup_help_dialog_string(HELP_IMPROVEMENTS_ITEM);
      break;
    case IDM_HELP_UNITS:
      popup_help_dialog_string(HELP_UNITS_ITEM);
    break;
    case IDM_HELP_COMBAT:
      popup_help_dialog_string(HELP_COMBAT_ITEM);
      break;
    case IDM_HELP_ZOC:
      popup_help_dialog_string(HELP_ZOC_ITEM);
      break;
    case IDM_HELP_TECH:
      popup_help_dialog_string(HELP_TECHS_ITEM);
      break;
    case IDM_HELP_TERRAIN:
      popup_help_dialog_string(HELP_TERRAIN_ITEM);
      break;
    case IDM_HELP_WONDERS:
      popup_help_dialog_string(HELP_WONDERS_ITEM);
      break;
    case IDM_HELP_GOVERNMENT:
      popup_help_dialog_string(HELP_GOVERNMENT_ITEM);
      break;
    case IDM_HELP_HAPPINESS:
      popup_help_dialog_string(HELP_HAPPINESS_ITEM);
      break;
    case IDM_HELP_SPACE_RACE:
      popup_help_dialog_string(HELP_SPACE_RACE_ITEM);
      break;
    case IDM_HELP_COPYING:
      popup_help_dialog_string(HELP_COPYING_ITEM);
      break;
    case IDM_HELP_ABOUT:
      popup_help_dialog_string(HELP_ABOUT_ITEM);
      break; 
    case IDOK:
      handle_chatline();
      break;
    default:
      break;
    }
}


/**************************************************************************

**************************************************************************/
static void my_rename_menu(HMENU hmenu,int id,char *newstr)
{
  ModifyMenu(hmenu,id,MF_BYCOMMAND|MF_STRING,id,newstr);
}

/**************************************************************************

**************************************************************************/
HMENU create_mainmenu()
{
  HMENU menu;
  menu=CreateMenu();
  AppendMenu(menu,MF_POPUP,(UINT)my_create_menu(game_menu),_("Game"));
  AppendMenu(menu,MF_POPUP,(UINT)my_create_menu(kingdom_menu),_("Kingdom"));
  AppendMenu(menu,MF_POPUP,(UINT)my_create_menu(view_menu),_("View"));
  AppendMenu(menu,MF_POPUP,(UINT)my_create_menu(orders_menu),_("Orders"));
  AppendMenu(menu,MF_POPUP,(UINT)my_create_menu(reports_menu),_("Reports")); 
  AppendMenu(menu,MF_POPUP,(UINT)my_create_menu(help_menu),_("Help"));
  return menu;
}

/**************************************************************************

**************************************************************************/
void
update_menus(void)
{
  enum MenuID id;
  HMENU hmenu;
  hmenu=GetMenu(root_window);
  if (get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    {
      for(id=IDM_KINGDOM_MENU+1;id<IDM_HELP_MENU;id++)
	my_enable_menu(hmenu,id,FALSE);
      
      my_enable_menu(hmenu,IDM_GAME_LOCAL_OPT,FALSE);
      my_enable_menu(hmenu,IDM_GAME_MESSAGE_OPT,FALSE);
      my_enable_menu(hmenu,IDM_GAME_SAVE_SETTINGS,FALSE);
      my_enable_menu(hmenu,IDM_GAME_PLAYERS,FALSE);
      my_enable_menu(hmenu,IDM_GAME_SERVER_INIT,TRUE);
      my_enable_menu(hmenu,IDM_GAME_SERVER_OPT_ON,TRUE);
      my_enable_menu(hmenu,IDM_GAME_EXPORT,TRUE);
      my_enable_menu(hmenu,IDM_GAME_CLEAR,TRUE);
      my_enable_menu(hmenu,IDM_GAME_DISCONNECT,TRUE);
    }
  else
    {
      struct unit *punit;
      for(id=IDM_KINGDOM_MENU+1;id<IDM_HELP_MENU;id++)
	my_enable_menu(hmenu,id,TRUE);
      
      my_enable_menu(hmenu,IDM_GAME_LOCAL_OPT,TRUE);
      my_enable_menu(hmenu,IDM_GAME_MESSAGE_OPT,TRUE);
      my_enable_menu(hmenu,IDM_GAME_SAVE_SETTINGS,TRUE);
      my_enable_menu(hmenu,IDM_GAME_PLAYERS,TRUE);
      my_enable_menu(hmenu,IDM_GAME_SERVER_INIT,TRUE);
      my_enable_menu(hmenu,IDM_GAME_SERVER_OPT_ON,TRUE);
      my_enable_menu(hmenu,IDM_GAME_EXPORT,TRUE);
      my_enable_menu(hmenu,IDM_GAME_CLEAR,TRUE);
      my_enable_menu(hmenu,IDM_GAME_DISCONNECT,TRUE);
      
      my_enable_menu(hmenu,IDM_REPORTS_SPACESHIP,
		     game.player_ptr->spaceship.state=!SSHIP_NONE);
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
	
        my_enable_menu(hmenu,IDM_ORDERS_AUTOSETTLER,
                          (can_unit_do_auto(punit)
                           && unit_flag(punit->type, F_SETTLERS)));
	
	my_enable_menu(hmenu,IDM_ORDERS_AUTOATTACK,
		       (can_unit_do_auto(punit)
			&& !unit_flag(punit->type, F_SETTLERS)));
	
	my_enable_menu(hmenu,IDM_ORDERS_BUILDCITY,
		       ((punit) || unit_can_help_build_wonder_here(punit) || 
			can_unit_add_or_build_city(punit)));
	my_enable_menu(hmenu,IDM_ORDERS_FORTRESS,
		       can_unit_do_activity(punit, ACTIVITY_FORTRESS));
	my_enable_menu(hmenu,IDM_ORDERS_AIRBASE,
		       can_unit_do_activity(punit, ACTIVITY_AIRBASE));
	my_enable_menu(hmenu,IDM_ORDERS_ROAD,
		       can_unit_do_activity(punit, ACTIVITY_ROAD) ||
		       can_unit_do_activity(punit, ACTIVITY_RAILROAD));
	my_enable_menu(hmenu,IDM_ORDERS_CONNECT,
		       can_unit_do_connect(punit, ACTIVITY_IDLE));
	my_enable_menu(hmenu,IDM_ORDERS_POLLUTION,
		       can_unit_do_activity(punit, ACTIVITY_POLLUTION) ||
		       can_unit_paradrop(punit));

	my_enable_menu(hmenu,IDM_ORDERS_WAKEUP,
		       is_unit_activity_on_tile(ACTIVITY_SENTRY,
						punit->x, punit->y));
	my_enable_menu(hmenu,IDM_ORDERS_WONDER,
		       unit_can_help_build_wonder_here(punit));       
	my_enable_menu(hmenu,IDM_ORDERS_TROUTE,
		       unit_can_est_traderoute_here(punit));
	if (unit_flag(punit->type,F_CITIES)
	    && map_get_city(punit->x,punit->y))
	  {
	    my_rename_menu(hmenu,IDM_ORDERS_BUILDCITY,_("Add to City"));
	  }
	else
	  {
	    my_rename_menu(hmenu,IDM_ORDERS_BUILDCITY,_("Build City"));
	  }
	ttype = map_get_tile(punit->x, punit->y)->terrain;
	tinfo = get_tile_type(ttype);    
	if ((tinfo->irrigation_result != T_LAST) && (tinfo->irrigation_result != ttype))
	  {
	    my_snprintf (irrtext, sizeof(irrtext), chgfmt,
			 (get_tile_type(tinfo->irrigation_result))->terrain_name);
	  }
	else if ((map_get_tile(punit->x,punit->y)->special&S_IRRIGATION) &&
		 player_knows_techs_with_flag(game.player_ptr, TF_FARMLAND))
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
	  my_rename_menu(hmenu,IDM_ORDERS_POLLUTION, _("Paradrop"));
	} else {
	  my_rename_menu(hmenu,IDM_ORDERS_POLLUTION, _("Clean Pollution"));
	}       
	my_rename_menu(hmenu,IDM_ORDERS_IRRIGATION,irrtext);
	my_rename_menu(hmenu,IDM_ORDERS_FOREST,mintext);
	my_rename_menu(hmenu,IDM_ORDERS_TRANSFORM,transtext);
	if (map_get_tile(punit->x,punit->y)->special&S_ROAD) {
	  roadtext = _("Build Railroad");
	  road_activity=ACTIVITY_RAILROAD;
	} else {
	  roadtext = _("Build Road");
	  road_activity=ACTIVITY_ROAD;
	}     
	my_rename_menu(hmenu,IDM_ORDERS_ROAD,roadtext);
	my_enable_menu(hmenu,IDM_ORDERS_FALLOUT,
		       can_unit_do_activity(punit, ACTIVITY_FALLOUT));
	my_enable_menu(hmenu,IDM_ORDERS_FORTIFY,
		       can_unit_do_activity(punit, ACTIVITY_FORTIFYING));
	my_enable_menu(hmenu,IDM_ORDERS_SENTRY,
		       can_unit_do_activity(punit, ACTIVITY_SENTRY));
	my_enable_menu(hmenu,IDM_ORDERS_PILLAGE,
		       can_unit_do_activity(punit, ACTIVITY_PILLAGE));
	my_enable_menu(hmenu,IDM_ORDERS_AUTOEXPLORE,
		       can_unit_do_activity(punit, ACTIVITY_EXPLORE));
	my_enable_menu(hmenu,IDM_ORDERS_FOREST,
		       can_unit_do_activity(punit, ACTIVITY_MINE));
	my_enable_menu(hmenu,IDM_ORDERS_IRRIGATION,
		       can_unit_do_activity(punit, ACTIVITY_IRRIGATE));
	my_enable_menu(hmenu,IDM_ORDERS_TRANSFORM,
		       can_unit_do_activity(punit, ACTIVITY_TRANSFORM));
	my_enable_menu(hmenu,IDM_ORDERS_HOME,
		       can_unit_change_homecity(punit));
	my_enable_menu(hmenu,IDM_ORDERS_EXPLODE,
		       unit_flag(punit->type, F_NUCLEAR));
	my_enable_menu(hmenu,IDM_ORDERS_UNLOAD,
		       get_transporter_capacity(punit)>0);
      }
      CheckMenuItem(hmenu,IDM_VIEW_GRID,MF_BYCOMMAND |
		    (draw_map_grid?MF_CHECKED:MF_UNCHECKED));
      CheckMenuItem(hmenu,IDM_VIEW_NAMES,MF_BYCOMMAND |
		    (draw_city_names?MF_CHECKED:MF_UNCHECKED));
      CheckMenuItem(hmenu,IDM_VIEW_PROD,MF_BYCOMMAND |
		    (draw_city_productions?MF_CHECKED:MF_UNCHECKED));
      CheckMenuItem(hmenu,IDM_VIEW_TERRAIN,MF_BYCOMMAND |
		    (draw_terrain?MF_CHECKED:MF_UNCHECKED));
      my_enable_menu(hmenu,IDM_VIEW_COASTLINE,!draw_terrain);
      CheckMenuItem(hmenu,IDM_VIEW_COASTLINE,MF_BYCOMMAND |
		    (draw_coastline?MF_CHECKED:MF_UNCHECKED));
      CheckMenuItem(hmenu,IDM_VIEW_SPECIALS,MF_BYCOMMAND |
		    (draw_specials?MF_CHECKED:MF_UNCHECKED));
      CheckMenuItem(hmenu,IDM_VIEW_POLLUTION,MF_BYCOMMAND |
		    (draw_pollution?MF_CHECKED:MF_UNCHECKED));
      CheckMenuItem(hmenu,IDM_VIEW_CITIES,MF_BYCOMMAND |
		    (draw_cities?MF_CHECKED:MF_UNCHECKED));
      CheckMenuItem(hmenu,IDM_VIEW_UNITS,MF_BYCOMMAND |
		    (draw_units?MF_CHECKED:MF_UNCHECKED));
      my_enable_menu(hmenu,IDM_VIEW_FOCUS_UNIT,
		     !draw_units); 
      CheckMenuItem(hmenu,IDM_VIEW_FOCUS_UNIT,MF_BYCOMMAND |
		    (draw_focus_unit?MF_CHECKED:MF_UNCHECKED));
      CheckMenuItem(hmenu,IDM_VIEW_FOG_OF_WAR,MF_BYCOMMAND |
		    (draw_fog_of_war?MF_CHECKED:MF_UNCHECKED));
      
    }
}


