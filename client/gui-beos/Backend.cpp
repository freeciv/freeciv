/*
	Backend.cpp
	Copyright 1999 Be Do Have. All Rights Reserved.
*/

#include <support/Debug.h>
#include "Defs.hpp"
#include "Backend.hpp"
#include "App.hpp"
#include "MainWindow.hpp"

// @@@@ TEMPORARY: for NOT_FINISHED
#include <Alert.h>

extern "C" {
#include "civclient.h"
}

// SIGH -- global
Backend *backend;

//--------------------------------------------------------------

Backend::Backend()
	: BLooper( "backend" )
{
	Run();
}


Backend::~Backend()
{
}


void
Backend::MessageReceived( BMessage *msg )
{
	// redirect if necessary
    if ( msg->what >= UI_WHATS_first && msg->what <= UI_WHATS_last ) {
        ui->PostMessage(msg);
        return;
    }
    if ( msg->what >= APP_WHATS_first && msg->what <= APP_WHATS_last ) {
        app->PostMessage(msg);
        return;
    }

	// handle dialogs and other inputs
	int8 which_button;		// for dialogs
	if ( msg->FindInt8("button", &which_button ) != B_OK ) {
		which_button = -2;
	}

	switch (msg->what) {

	case PREGAME_STATE:
		// @@@@ handle any other backend-state-resetting
		set_client_state(CLIENT_PRE_GAME_STATE);
		break; // PREGAME_STATE

	case PLEASE_CONNECT_TO:
    	if ( which_button == 0 ) {
			const char *nameStr, *serverStr, *portStr;
			if ( msg->FindString( "data", 0, &nameStr ) != B_OK 
						|| ! nameStr || ! *nameStr ) {
				nameStr = NULL;
				msg->what = UI_POPUP_CONNECT_DIALOG;
				Message_ReplaceString( msg, "text", "Missing name!" );
				ui->PostMessage( msg );
			} else if ( msg->FindString( "data", 1, &serverStr ) != B_OK
						|| ! serverStr || ! *serverStr ) {
				serverStr = NULL;
				msg->what = UI_POPUP_CONNECT_DIALOG;
				Message_ReplaceString( msg, "text", "Missing server!" );
				ui->PostMessage( msg );
			} else if ( msg->FindString( "data", 2, &portStr ) != B_OK
						|| ! portStr || ! *portStr ) {
				portStr = NULL;
				msg->what = UI_POPUP_CONNECT_DIALOG;
				Message_ReplaceString( msg, "text", "Missing port!" );
				ui->PostMessage( msg );
			}

			if ( nameStr && serverStr && portStr ) {
		// @@@@
				NOT_FINISHED( nameStr );
			}
		}
		break; // PLEASE_CONNECT_TO

	case INPUTVIEW_GENERATES_TEXT:
		NOT_FINISHED( "INPUTVIEW_GENERATES_TEXT" );
		break;

	default:
		BLooper::MessageReceived(msg);
		break;
	}
}


bool
Backend::QuitRequested( void )
{
	return true;
}

