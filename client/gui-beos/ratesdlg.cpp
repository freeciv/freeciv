/* ratesdlg.cpp */

#include "Defs.hpp"
#include "MainWindow.hpp"
#include "ratesdlg.hpp"

void popup_rates_dialog( void )	// HOOK
{
	ui->PostMessage( UI_POPUP_RATES_DIALOG );
}


//---------------------------------------------------------------------
// Work functions
// @@@@

// UI_POPUP_RATES_DIALOG,
