/* spaceshipdlg.cpp */

#include <Message.h>
#include "Defs.hpp"
#include "MainWindow.hpp"
#include "spaceshipdlg.h"
#include "BdhDialog.h"

BdhDialog *spaceship_dialog = NULL;

void
popup_spaceship_dialog(struct player *pplayer)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_SPACESHIP_DIALOG );
    msg->AddPointer( "player", pplayer );
    ui->PostMessage( msg );
}


void
popdown_spaceship_dialog(struct player *pplayer)	// HOOK
{
	if (!spaceship_dialog) return;
    BMessage *msg = new BMessage( UI_POPDOWN_SPACESHIP_DIALOG );
    msg->AddPointer( "player", pplayer );
    ui->PostMessage( msg );
}


void
refresh_spaceship_dialog(struct player *pplayer)
{
    BMessage *msg = new BMessage( UI_REFRESH_SPACESHIP_DIALOG );
    msg->AddPointer( "player", pplayer );
    ui->PostMessage( msg );
}



//---------------------------------------------------------------------
// Work functions
// @@@@
#include <Alert.h>

// UI_POPUP_SPACESHIP_DIALOG,
// UI_POPDOWN_SPACESHIP_DIALOG,
// UI_REFRESH_SPACESHIP_DIALOG,
