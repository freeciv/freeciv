/*
	MainWindow.cpp
	Copyright (c) 1997 Be Do Have Software. All rights reserved.
 */

#include <stdio.h>
#include <support/Debug.h>

#include <Alert.h>	// temporary

#include "MainWindow.hpp"
#include "App.hpp"
#include "Defs.hpp"
#include "Backend.hpp"

#include "RadarView.hpp"
#include "SummaryView.hpp"
#include "StatusView.hpp"
#include "UnitInfoView.hpp"
#include "UnitsBelowView.hpp"
#include "MapCanvas.hpp"
#include "InputView.hpp"
#include "OutputView.hpp"

#include "connectdlg.hpp"
#include "finddlg.hpp"
#include "helpdlg.hpp"
#include "menu.hpp"
#include "messagedlg.hpp"
#include "optiondlg.hpp"
#include "ratesdlg.hpp"
#include "wldlg.hpp"

#include "chatline.h"
#include "cityrep.h"
#include "dialogs.h"
#include "gotodlg.h"
#include "mapctrl.h"
#include "messagewin.h"
#include "plrdlg.h"
#include "repodlgs.h"
#include "spaceshipdlg.h"

extern "C" {
#include "civclient.h"
#include "clinet.h"
#include "control.h"
#include "options.h"
}

// SIGH -- global
MainWindow *ui;


// ---- MainWindow -------------------------------------------

MainWindow::MainWindow(void)
	: BdhMenuedWindow( BPoint(20,20),
			APP_NAME, B_NOT_RESIZABLE | B_NOT_ZOOMABLE )
{
	Init();

 	// set default preferences if none available
	if ( app->appPrefs()->InitCheck() != B_OK ) { 
	}

	// finalize bitmaps 
	// @@@@

	setup_menus();
}


void
MainWindow::Init( void )
{
// stack left: radar, own summary, status indicator cluster, turn-done button,
//                      unit info, unit-set/stack graphics cluster
        radar = new RadarView( radar_gfx_sprite );
        viewList->addAtTop( radar );
        summary = new SummaryView( radar->Width() );
        viewList->addAtBottom( summary );
        status =  new StatusView( radar->Width());
        viewList->addAtBottom( status );
        unitInfo = new UnitInfoView( radar->Width() );
        viewList->addAtBottom( unitInfo );
        unitsBelow = new UnitsBelowView( radar->Width() );
        viewList->addAtBottom( unitsBelow );

// map screen right (with horiz/vert scrollbars)
        map = new MapCanvas( 17, 10, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT );
        viewList->addAtRight( map );

// incoming message textarea next to bottom
		float wide = viewList->frame().Width();
        output = new OutputView( be_plain_font, wide );
        viewList->addAtBottom( output );

// outgoing message text line at bottom
        input = new InputView( be_plain_font, wide );
        viewList->addAtBottom( input );

	BdhMenuedWindow::Init();
}


MainWindow::~MainWindow()
{
}

void
MainWindow::MessageReceived( BMessage *msg )
{
	// redirect if necessary
    if ( msg->what >= BACKEND_WHATS_first && msg->what <= BACKEND_WHATS_last ) {
        backend->PostMessage(msg);
        return;
    }
    if ( msg->what >= APP_WHATS_first && msg->what <= APP_WHATS_last ) {
        app->PostMessage(msg);
        return;
    }

	switch (msg->what) {
	// -- requests handling dialogs -------------------------------
	case UI_POPUP_CONNECT_DIALOG:
		DetachCurrentMessage();
		popup_connect_dialog( msg );
		break;

	// -- requests triggering main-window effects -----------------
	case UI_UPDATE_MENUS:
		MainWindow::update_menus();
		break;

	case UI_APPEND_OUTPUT_WINDOW:
	  {
		const char *str = msg->FindString("string");
		output->Append( str );
	  }
		break;

	case UI_LOG_OUTPUT_WINDOW:
		output->ExportToLog();
		break;

	case UI_CLEAR_OUTPUT_WINDOW:
		output->Clear();
		break;

	case UI_POPUP_CITY_DIALOG:
	case UI_POPDOWN_CITY_DIALOG:
	case UI_REFRESH_CITY_DIALOG:
	case UI_REFRESH_UNIT_CITY_DIALOGS:
	case UI_POPUP_CITY_REPORT_DIALOG:
	case UI_UPDATE_CITY_REPORT_DIALOG:
	case UI_POPUP_NOTIFY_GOTO_DIALOG:
	case UI_POPUP_NOTIFY_DIALOG:
	case UI_POPUP_RACES_DIALOG:
	case UI_POPDOWN_RACES_DIALOG:
	case UI_POPUP_UNIT_SELECT_DIALOG:
	case UI_RACES_TOGGLES_SET_SENSITIVE_DIALOG:
	case UI_POPUP_REVOLUTION_DIALOG:
	case UI_POPUP_GOVERNMENT_DIALOG:
	case UI_POPUP_CARAVAN_DIALOG:
	case UI_POPUP_DIPLOMAT_DIALOG:
	case UI_POPUP_INCITE_DIALOG:
	case UI_POPUP_BRIBE_DIALOG:
	case UI_POPUP_PILLAGE_DIALOG:
	case UI_POPUP_SABOTAGE_DIALOG:
	case UI_POPUP_UNIT_CONNECT_DIALOG:
	case UI_HANDLE_DIPLO_ACCEPT_TREATY:
	case UI_HANDLE_DIPLO_INIT_MEETING:
	case UI_HANDLE_DIPLO_CREATE_CLAUSE:
	case UI_HANDLE_DIPLO_CANCEL_MEETING:
	case UI_HANDLE_DIPLO_REMOVE_CLAUSE:
	case UI_POPUP_FIND_DIALOG:
	case UI_POPUP_GOTO_DIALOG:
	case UI_POPUP_HELP_DIALOG:
	case UI_POPUP_HELP_DIALOG_STRING:
	case UI_POPUP_HELP_DIALOG_TYPED:
	case UI_POPDOWN_HELP_DIALOG: 
	case UI_POPUP_INTEL_DIALOG:
	case UI_POPUP_NEWCITY_DIALOG:
	case UI_SET_TURN_DONE_BUTTON_STATE:
	case UI_CENTER_ON_UNIT:
	case UI_UPDATE_INFO_LABEL:
	case UI_UPDATE_UNIT_INFO_LABEL:
	case UI_UPDATE_TIMEOUT_LABEL:
	case UI_UPDATE_UNIT_PIX_LABEL:
	case UI_UPDATE_TURN_DONE_BUTTON:
	case UI_SET_BULB_SOL_GOV:
	case UI_SET_OVERVIEW_DIMENSIONS:
	case UI_OVERVIEW_UPDATE_TILE:
	case UI_CENTER_TILE_MAPCANVAS:
	case UI_UPDATE_MAP_CANVAS:
	case UI_UPDATE_MAP_CANVAS_VISIBLE:
	case UI_UPDATE_MAP_CANVAS_SCROLLBARS:
	case UI_UPDATE_CITY_DESCRIPTIONS:
	case UI_PUT_CROSS_OVERLAY_TILE:
	case UI_PUT_CITY_WORKERS:
	case UI_MOVE_UNIT_MAP_CANVAS:
	case UI_DECREASE_UNIT_HP:
	case UI_PUT_NUKE_MUSHROOM_PIXMAPS:
	case UI_REFRESH_OVERVIEW_CANVAS:
	case UI_REFRESH_OVERVIEW_VIEWRECT:
	case UI_REFRESH_TILE_MAPCANVAS:
	case UI_POPUP_MESSAGEOPT_DIALOG:
	case UI_POPUP_MESWIN_DIALOG:
	case UI_UPDATE_MESWIN_DIALOG:
	case UI_CLEAR_NOTIFY_WINDOW:
	case UI_ADD_NOTIFY_WINDOW:
	case UI_MESWIN_UPDATE_DELAY: 
	case UI_POPUP_OPTION_DIALOG:
	case UI_POPUP_PLAYERS_DIALOG:
	case UI_UPDATE_PLAYERS_DIALOG:
	case UI_POPUP_RATES_DIALOG:
	case UI_REPORT_UPDATE_DELAY:
	case UI_UPDATE_REPORT_DIALOGS:
	case UI_UPDATE_SCIENCE_DIALOG:
	case UI_POPUP_SCIENCE_DIALOG:
	case UI_UPDATE_TRADE_REPORT_DIALOG:
	case UI_POPUP_TRADE_REPORT_DIALOG:
	case UI_UPDATE_ACTIVEUNITS_REPORT_DIALOG:
	case UI_POPUP_ACTIVEUNITS_REPORT_DIALOG:
	case UI_POPUP_SPACESHIP_DIALOG:
	case UI_POPDOWN_SPACESHIP_DIALOG:
	case UI_REFRESH_SPACESHIP_DIALOG: 
	case UI_POPUP_WORKLISTS_DIALOG:
	case UI_UPDATE_WORKLIST_REPORT_DIALOG:
		// @@@@ write work functions
		output->Append( "Hooked" );
		break;

	// -- APP menu -------------------------------------------------
	// @@@@ walk all menu items, redirecting as necessary
	case MENU_GAME_OPTIONS:
		popup_option_dialog();
		break;

	case MENU_GAME_MSG_OPTIONS:
		popup_messageopt_dialog();
		break;

	case MENU_GAME_SAVE_SETTINGS:
		// save_options();
		// break;

	case MENU_GAME_PLAYERS:
		popup_players_dialog();
		break;

	case MENU_GAME_MESSAGES:
		popup_meswin_dialog();
		break;

	case MENU_GAME_SERVER_OPTIONS1:
		// send_report_request(REPORT_SERVER_OPTIONS1);
		// break;

	case MENU_GAME_SERVER_OPTIONS2:
		// send_report_request(REPORT_SERVER_OPTIONS2);
		// break;

	case MENU_GAME_OUTPUT_LOG:
		log_output_window();
		break;

	case MENU_GAME_CLEAR_OUTPUT:
		clear_output_window();
		break;

	case MENU_GAME_DISCONNECT:
		// disconnect_from_server();
		// break;

		NOT_FINISHED( "MENU UNHOOKED" );
		break;


	// -- Kingdom menu -------------------------------------------------
	case MENU_KINGDOM_RATES:
		popup_rates_dialog();
		break;

	case MENU_KINGDOM_FIND_CITY:
		popup_find_dialog();
		break;

	case MENU_KINGDOM_WORKLISTS:
		popup_worklists_dialog(game.player_ptr);
		break;

	case MENU_KINGDOM_REVOLUTION:
		popup_revolution_dialog();
		break;


	// -- View menu -------------------------------------------------
	case MENU_VIEW_SHOW_MAP_GRID:
		// key_map_grid_toggle();
		// break;

	case MENU_VIEW_CENTER_VIEW:
		// request_center_focus_unit();
		// break;
 
		NOT_FINISHED( "MENU UNHOOKED" );
		break;


	// -- Orders menu -------------------------------------------------
	case MENU_ORDER_BUILD_CITY:
		// key_unit_build_city();
		// break;

	case MENU_ORDER_ROAD:
		// key_unit_road();
		// break;

	case MENU_ORDER_IRRIGATE:
		// key_unit_irrigate();
		// break;

	case MENU_ORDER_MINE:
		// key_unit_mine();
		// break;

	case MENU_ORDER_TRANSFORM:
		// key_unit_transform();
		// break;

	case MENU_ORDER_FORTRESS:
		// key_unit_fortress();
		// break;

	case MENU_ORDER_AIRBASE:
		// key_unit_airbase();
		// break;

    case MENU_ORDER_PARADROP:
		// key_unit_paradrop();
		// break; 

	case MENU_ORDER_POLLUTION:
		// key_unit_pollution();
		// break;

	case MENU_ORDER_FORTIFY:
		// key_unit_fortify();
		// break;

	case MENU_ORDER_SENTRY:
		// key_unit_sentry();
		// break;

	case MENU_ORDER_PILLAGE:
		// key_unit_pillage();
		// break;

	case MENU_ORDER_HOMECITY:
		// key_unit_homecity();
		// break;

	case MENU_ORDER_UNLOAD:
		// key_unit_unload();
		// break;

	case MENU_ORDER_WAKEUP_OTHERS:
		// key_unit_wakeup_others();
		// break;

	case MENU_ORDER_AUTO_SETTLER:
		// key_unit_auto_settle();
		// break;

	case MENU_ORDER_AUTO_ATTACK:
		// key_unit_auto_attack();
		// break;

	case MENU_ORDER_AUTO_EXPLORE:
		// key_unit_auto_explore();
		// break;

	case MENU_ORDER_CONNECT:
		// key_unit_connect();
		// break;

	case MENU_ORDER_GOTO:
		// key_unit_goto();
		// break;

	case MENU_ORDER_GOTO_CITY:
		// if(get_unit_in_focus())
      		// popup_goto_dialog();
		// break;

	case MENU_ORDER_DISBAND:
		// key_unit_disband();
		// break;

	case MENU_ORDER_BUILD_WONDER:
		// key_unit_build_wonder();
		// break;

	case MENU_ORDER_TRADEROUTE:
		// key_unit_traderoute();
		// break;

	case MENU_ORDER_NUKE:
		// key_unit_nuke();
		// break;

	case MENU_ORDER_WAIT:
		// key_unit_wait();
		// break;

	case MENU_ORDER_DONE:
		// key_unit_done();
		// break;

		NOT_FINISHED( "MENU UNHOOKED" );
		break;


	// -- Reports menu -------------------------------------------------
	case MENU_REPORT_CITY:
		popup_city_report_dialog(0);
		break;

	case MENU_REPORT_MILITARY:
		popup_activeunits_report_dialog(0);
		break;

	case MENU_REPORT_TRADE:
		popup_trade_report_dialog(0);
		break;

	case MENU_REPORT_SCIENCE:
		popup_science_dialog(0);
		break;

	case MENU_REPORT_WOW:
		// send_report_request(REPORT_WONDERS_OF_THE_WORLD);
		// break;

	case MENU_REPORT_TOP_CITIES:
		// send_report_request(REPORT_TOP_5_CITIES);
		// break;

	case MENU_REPORT_DEMOGRAPHIC:
		// send_report_request(REPORT_DEMOGRAPHIC);
		// break;

		NOT_FINISHED( "MENU UNHOOKED" );
		break;

	case MENU_REPORT_SPACESHIP:
		popup_spaceship_dialog(game.player_ptr);
		break;


	// -- Help menu -------------------------------------------------
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
		

	// -- anything else is Somebody Else's Problem .... ---------------
 	default:
		BdhMenuedWindow::MessageReceived(msg);
		break;
	}
}

bool
MainWindow::QuitRequested( void )
{
	app->PostMessage( B_QUIT_REQUESTED );
	// @@@@ HANDLE OTHER SHUTDOWN HERE?
	return true;
}


void
MainWindow::GotoDocUrl( const char *str )
{
	char url[256];
	strcpy( url, "file://" );
	strcat( url, appDirpath() );
	strcat( url, "/docs/" );
	strcat( url, str );
	app->GotoUrl( url );
}

void
MainWindow::setup_menus(void)
{
	BMenu *menu;
	int   i;

	// extend app menu
	i = 1;
	AddItem( new BMenuItem( "Local Options",
							new BMessage( MENU_GAME_OPTIONS ) ),
			 0, i++ );
	AddItem( new BMenuItem( "Message Options",
							new BMessage( MENU_GAME_MSG_OPTIONS ) ),
			 0, i++ );
	AddItem( new BMenuItem( "Save Settings",
							new BMessage( MENU_GAME_SAVE_SETTINGS ) ),
			 0, i++ );
	AddItem( new BSeparatorItem(), 0, i++ );
	AddItem( new BMenuItem( "Players",
							new BMessage( MENU_GAME_PLAYERS ) ),
			 0, i++ );
	AddItem( new BMenuItem( "Messages",
							new BMessage( MENU_GAME_MESSAGES ) ),
			 0, i++ );
	AddItem( new BMenuItem( "Server Options (initial)",
							new BMessage( MENU_GAME_SERVER_OPTIONS1 ) ),
			 0, i++ );
	AddItem( new BMenuItem( "Server Options (ongoing)",
							new BMessage( MENU_GAME_SERVER_OPTIONS2 ) ),
			 0, i++ );
	AddItem( new BSeparatorItem(), 0, i++ );
	AddItem( new BMenuItem( "Export Log",
							new BMessage( MENU_GAME_OUTPUT_LOG ) ),
			 0, i++ );
	AddItem( new BMenuItem( "Clear Output",
							new BMessage( MENU_GAME_CLEAR_OUTPUT ) ),
			 0, i++ );
	AddItem( new BSeparatorItem(), 0, i++ );
	AddItem( new BMenuItem( "Disconnect",
							new BMessage( MENU_GAME_DISCONNECT ) ),
			 0, i++ );

	// add Kingdom menu
	i = 0;
	menu = new BMenu("Kingdom");
	AddItem( menu, 1 );
	AddItem( new BMenuItem( "Tax Rates",
							new BMessage( MENU_KINGDOM_RATES ) ),
			 1, i++ );
	AddItem( new BMenuItem( "Find City",
							new BMessage( MENU_KINGDOM_FIND_CITY ) ),
			 1, i++ );
	AddItem( new BMenuItem( "Worklists",
							new BMessage( MENU_KINGDOM_WORKLISTS ) ),
			 1, i++ );
	AddItem( new BMenuItem( "Revolution",
							new BMessage( MENU_KINGDOM_REVOLUTION ) ),
			 1, i++ );

	//		View
	i = 0;
	menu = new BMenu("View");
	AddItem( menu, 2 );
	AddItem( new BMenuItem( "Map Grid",
							new BMessage( MENU_VIEW_SHOW_MAP_GRID ) ),
			 2, i++ );
	AddItem( new BMenuItem( "Center View",
							new BMessage( MENU_VIEW_CENTER_VIEW ) ),
			 2, i++ );

	//		Orders
	i = 0;
	menu = new BMenu("Orders");
	AddItem( menu, 3 );
	AddItem( new BMenuItem( "Build City",
							new BMessage( MENU_ORDER_BUILD_CITY ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Build Road",
							new BMessage( MENU_ORDER_ROAD ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Irrigate",
							new BMessage( MENU_ORDER_IRRIGATE ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Build Mine",
							new BMessage( MENU_ORDER_MINE ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Transform Terrain",
							new BMessage( MENU_ORDER_TRANSFORM ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Build Fortress",
							new BMessage( MENU_ORDER_FORTRESS ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Build Airbase",
							new BMessage( MENU_ORDER_AIRBASE ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Parachute Drop",
							new BMessage( MENU_ORDER_PARADROP ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Clean Pollution",
							new BMessage( MENU_ORDER_POLLUTION ) ),
			 3, i++ );
	AddItem( new BSeparatorItem(), 3, i++ );
	AddItem( new BMenuItem( "Fortify",
							new BMessage( MENU_ORDER_FORTIFY ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Sentry",
							new BMessage( MENU_ORDER_SENTRY ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Pillage",
							new BMessage( MENU_ORDER_PILLAGE ) ),
			 3, i++ );
	AddItem( new BSeparatorItem(), 3, i++ );
	AddItem( new BMenuItem( "Make Homecity",
							new BMessage( MENU_ORDER_HOMECITY ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Unload",
							new BMessage( MENU_ORDER_UNLOAD ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Wake up Others",
							new BMessage( MENU_ORDER_WAKEUP_OTHERS ) ),
			 3, i++ );
	AddItem( new BSeparatorItem(), 3, i++ );
	AddItem( new BMenuItem( "Auto-settle",
							new BMessage( MENU_ORDER_AUTO_SETTLER ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Auto-attack",
							new BMessage( MENU_ORDER_AUTO_ATTACK ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Auto-explore",
							new BMessage( MENU_ORDER_AUTO_EXPLORE ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Connect",
							new BMessage( MENU_ORDER_CONNECT ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Go to",
							new BMessage( MENU_ORDER_GOTO ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Go/Airlift to City",
							new BMessage( MENU_ORDER_GOTO_CITY ) ),
			 3, i++ );
	AddItem( new BSeparatorItem(), 3, i++ );
	AddItem( new BMenuItem( "Disband Unit",
							new BMessage( MENU_ORDER_DISBAND ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Help Build Wonder",
							new BMessage( MENU_ORDER_BUILD_WONDER ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Make Trade Route",
							new BMessage( MENU_ORDER_TRADEROUTE ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Explode Nuclear",
							new BMessage( MENU_ORDER_NUKE ) ),
			 3, i++ );
	AddItem( new BSeparatorItem(), 3, i++ );
	AddItem( new BMenuItem( "Wait",
							new BMessage( MENU_ORDER_WAIT ) ),
			 3, i++ );
	AddItem( new BMenuItem( "Done",
							new BMessage( MENU_ORDER_DONE ) ),
			 3, i++ );

	//		Reports
	i = 0;
	menu = new BMenu("Reports");
	AddItem( menu, 4 );
	AddItem( new BMenuItem( "City Report",
							new BMessage( MENU_REPORT_CITY ) ),
			 4, i++ );
	AddItem( new BMenuItem( "Military Report",
							new BMessage( MENU_REPORT_MILITARY ) ),
			 4, i++ );
	AddItem( new BMenuItem( "Trade Report",
							new BMessage( MENU_REPORT_TRADE ) ),
			 4, i++ );
	AddItem( new BMenuItem( "Science Report",
							new BMessage( MENU_REPORT_SCIENCE ) ),
			 4, i++ );
	AddItem( new BSeparatorItem(), 4, i++ );
	AddItem( new BMenuItem( "Wonders of the World",
							new BMessage( MENU_REPORT_WOW ) ),
			 4, i++ );
	AddItem( new BMenuItem( "Top Five Cities",
							new BMessage( MENU_REPORT_TOP_CITIES ) ),
			 4, i++ );
	AddItem( new BMenuItem( "Demographics",
							new BMessage( MENU_REPORT_DEMOGRAPHIC ) ),
			 4, i++ );
	AddItem( new BMenuItem( "Spaceship",
							new BMessage( MENU_REPORT_SPACESHIP ) ),
			 4, i++ );

	// add Help menu
	i = 0;
	menu = new BMenu("Help");
	AddItem( menu, 5 );
	AddItem( new BMenuItem( "Connecting",
							new BMessage( MENU_HELP_CONNECTING ) ),
			 5, i++ );
	AddItem( new BMenuItem( "Controls",
							new BMessage( MENU_HELP_CONTROLS ) ),
			 5, i++ );
	AddItem( new BMenuItem( "Chatline",
							new BMessage( MENU_HELP_CHATLINE ) ),
			 5, i++ );
	AddItem( new BMenuItem( "Playing",
							new BMessage( MENU_HELP_PLAYING ) ),
			 5, i++ );
	AddItem( new BSeparatorItem(), 5, i++ );
	AddItem( new BMenuItem( "Improvements",
							new BMessage( MENU_HELP_IMPROVEMENTS ) ),
			 5, i++ );
	AddItem( new BMenuItem( "Units",
							new BMessage( MENU_HELP_UNITS ) ),
			 5, i++ );
	AddItem( new BMenuItem( "Combat",
							new BMessage( MENU_HELP_COMBAT ) ),
			 5, i++ );
	AddItem( new BMenuItem( "Zones of Control",
							new BMessage( MENU_HELP_ZOC ) ),
			 5, i++ );
	AddItem( new BMenuItem( "Technology",
							new BMessage( MENU_HELP_TECH ) ),
			 5, i++ );
	AddItem( new BMenuItem( "Terrain",
							new BMessage( MENU_HELP_TERRAIN ) ),
			 5, i++ );
	AddItem( new BMenuItem( "Wonders",
							new BMessage( MENU_HELP_WONDERS ) ),
			 5, i++ );
	AddItem( new BMenuItem( "Government",
							new BMessage( MENU_HELP_GOVERNMENT ) ),
			 5, i++ );
	AddItem( new BMenuItem( "Happiness",
							new BMessage( MENU_HELP_HAPPINESS ) ),
			 5, i++ );
	AddItem( new BMenuItem( "Space Race",
							new BMessage( MENU_HELP_SPACE_RACE ) ),
			 5, i++ );
	AddItem( new BSeparatorItem(), 5, i++ );
	AddItem( new BMenuItem( "Copying",
							new BMessage( MENU_HELP_COPYING ) ),
			 5, i++ );
	AddItem( new BMenuItem( "About Freeciv",
							new BMessage( MENU_HELP_ABOUT ) ),
			 5, i++ );
}


void
MainWindow::update_menus(void)
{
	NOT_FINISHED( "update_menus" );
}

