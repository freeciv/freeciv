/* inteldlg.cpp */

#include <Message.h>
#include "Defs.hpp"
#include "MainWindow.hpp"
extern "C" {
#include "inteldlg.h"
}

void popup_intel_dialog(struct player *p)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_INTEL_DIALOG );
    msg->AddPointer( "player", p );
    ui->PostMessage( msg );
}



//---------------------------------------------------------------------
// Work functions
// @@@@
#include <Alert.h>

// UI_POPUP_INTEL_DIALOG,
