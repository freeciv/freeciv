/* plrdlg.cpp */

#include "Defs.hpp"
#include "MainWindow.hpp"
#include "plrdlg.h"

void
popup_players_dialog(void)	// HOOK
{
	ui->PostMessage( UI_POPUP_PLAYERS_DIALOG );
}


void
update_players_dialog(void)	// HOOK
{
	ui->PostMessage( UI_UPDATE_PLAYERS_DIALOG );
}



//---------------------------------------------------------------------
// Work functions
// @@@@

// UI_POPUP_PLAYERS_DIALOG,
// UI_UPDATE_PLAYERS_DIALOG,
