/* finddlg.cpp */

#include <Message.h>
#include "Defs.hpp"
#include "MainWindow.hpp"
#include "finddlg.hpp"

void popup_find_dialog(void)	// HOOK
{
	ui->PostMessage( UI_POPUP_FIND_DIALOG );
}



//---------------------------------------------------------------------
// Work functions
// @@@@
#include <Alert.h>

// UI_POPUP_FIND_DIALOG,
