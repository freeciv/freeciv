/*
	App.cpp
	Copyright 1996-1997 Be Do Have. All Rights Reserved.
*/

#include <Alert.h>
#include <support/Debug.h>

#include <stdio.h>
#include <string.h>

extern "C" {
#include "civclient.h"
#include "clinet.h"
#include "config.h"
#include "fcintl.h"
#include "game.h"
#include "helpdata.h"
#include "log.h"
#include "mem.h"
#include "options.h"
#include "shared.h"
#include "tilespec.h"
#include "version.h"

/* STEALTH GLOBALS from client/civclient.c */
extern char     metaserver      [];
extern char     server_host     [];
extern char     name            [];
extern int		server_port;
/* STEALTH GLOBALS from gui-beos/gui_main.c */
extern char     logfile      [];
}

#include "App.hpp"
#include "Defs.hpp"
#include "MainWindow.hpp"
#include "graphics.hpp"
#include "Backend.hpp"

// SIGH -- globals 
App *app;


//--------------------------------------------------------------

App::App()
	: BdhApp( APP_SIGNATURE )
{
	backend = NULL;
	ui = NULL;
}


App::~App()
{
}


void
App::MessageReceived( BMessage *msg )
{
	// redirect if necessary
	if ( msg->what >= UI_WHATS_first && msg->what <= UI_WHATS_last ) {
		ui->PostMessage(msg);
		return;
	}
	if ( msg->what >= BACKEND_WHATS_first && msg->what <= BACKEND_WHATS_last ) {
		backend->PostMessage(msg);
		return;
	}

	// handle own stuff
	switch (msg->what) {
	default:
		BdhApp::MessageReceived(msg);
		break;
	} // switch
}


void
App::ReadyToRun( void )
{
	BdhApp::ReadyToRun();

	// finish setup required before main window
	tilespec_load_tiles();
	load_intro_gfx();
	load_cursors();
	load_options();

	// set up main window and show it
	ui = new MainWindow();
	if ( ! ui ) {
		BAlert *a = new BAlert( APP_NAME,
					"Could not construct main window!!", "ARGH!" );
		a->Go();
		PostMessage( B_QUIT_REQUESTED );
		return;
	}
	ui->Show();

	// complete initializations and trigger connect dialog
	backend->PostMessage(PREGAME_STATE);
}


void
App::AboutRequested( void )
{
	BAlert *alert = new BAlert( APP_NAME,
		APP_NAME " " APP_VERSION_STRING " \n\n" \
		COPYRIGHT_STRING " \n" LICENSE_TYPE_TEXT " \n" \
		LICENSE_COPY1_TEXT " " LICENSE_COPY2_TEXT " \n" \
		LICENSE_THANKS_TEXT "\n\n" ABOUT_BOX_CREDITS_TEXT,
		"Primo!" );
	alert->Go();
}


bool
App::QuitRequested( void )
{
	return BdhApp::QuitRequested();
}


//============================================================================

// -- Start the visible application.
//	NOTE:  args get handled by generic client code
void
app_main( int argc, char** argv )
{
	// Set up all threads, then trigger
	app = new App();
	backend = new Backend();
    app->Run();

	// Clean up
	if ( backend && backend->Lock() ) {
		backend->Quit();
	}
	delete backend;
	backend = NULL;
    delete app;
}
