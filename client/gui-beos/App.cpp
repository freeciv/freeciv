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
// #include "MainWindow.hpp"
#include "graphics.hpp"
// #include "Backend.hpp"

// SIGH -- globals 
App *app;


//--------------------------------------------------------------

App::App()
	: BApplication( APP_SIGNATURE ), mainWin(0)
{
}


App::~App()
{
}


void
App::MessageReceived( BMessage *msg )
{
	switch (msg->what) {
	default:
		BApplication::MessageReceived(msg);
		break;
	} // switch
}


void
App::ReadyToRun( void )
{
	BApplication::ReadyToRun();

	// finish setup required before main window
	tilespec_load_tiles();
	load_intro_gfx();
	load_cursors();
	load_options();
	set_client_state(CLIENT_PRE_GAME_STATE);

	AboutRequested();
	app->PostMessage( B_QUIT_REQUESTED );
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
	return true;
}


//============================================================================

// -- Start the visible application.
//	NOTE:  args get delivered via other mechanism, to App::ArgvReceived()
void
app_main( int argc, char *argv[] )
{
	// Start the UI itself
	app = new App();
    app->Run();

    delete app;
	fprintf( stderr, "FreeCiv rules!\n" );
}

