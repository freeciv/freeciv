/* connectdlg.cpp */

#include <Alert.h>
#include "Defs.hpp"
#include "connectdlg.hpp"

#include "App.hpp"
#include "Backend.hpp"
#include "MainWindow.hpp"

#include "BdhEntryDialog.h"

void
gui_server_connect(void)	// HOOK
{
	ui->PostMessage( UI_POPUP_CONNECT_DIALOG );
}

void
popup_connect_dialog( BMessage *specMsg )	// MainWindow thread
{
	specMsg->what = PLEASE_CONNECT_TO;
	if ( ! specMsg->HasString("text") ) {
		specMsg->AddString( "text", "Connect to which server?" );
	}
	if ( ! specMsg->HasString("label",0) ) {
		specMsg->AddString( "label", "Your Name:" );
		specMsg->AddString( "label", "Server:" );
		specMsg->AddString( "label", "Port:" );
	}
	BdhEntryDialog *cd = new BdhEntryDialog( BPoint(150,150), 
			specMsg, BDH_ICON_APP );
	cd->Go( backend );
}
