/* gotodlg.cpp */

#include "MainWindow.hpp"
#include "Defs.hpp"
#include "gotodlg.h"

void popup_goto_dialog(void)	// HOOK
{
	ui->PostMessage( UI_POPUP_GOTO_DIALOG );
}



//---------------------------------------------------------------------
// Work functions
// @@@@
#include <Alert.h>

// UI_POPUP_GOTO_DIALOG

