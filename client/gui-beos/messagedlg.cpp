/* messagedlg.cpp */

#include "Defs.hpp"
#include "MainWindow.hpp"

void popup_messageopt_dialog( void )	// HOOK
{
	ui->PostMessage( UI_POPUP_MESSAGEOPT_DIALOG );
}



//---------------------------------------------------------------------
// Work functions
// @@@@
#include <Alert.h>

// UI_POPUP_MESSAGEOPT_DIALOG,
