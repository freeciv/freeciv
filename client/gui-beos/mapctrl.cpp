/* mapctrl.cpp */

#include <Message.h>
#include "Defs.hpp"
#include "MainWindow.hpp"
#include "mapctrl.h"

struct city *city_workers_display = 0;

void
popup_newcity_dialog(struct unit *punit, char *suggestname)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_NEWCITY_DIALOG );
    msg->AddPointer( "unit", punit );
	msg->AddString( "suggest", suggestname );
    ui->PostMessage( msg );
}


void set_turn_done_button_state(bool state)	// HOOK
{
    BMessage *msg = new BMessage( UI_SET_TURN_DONE_BUTTON_STATE );
    msg->AddInt32( "state", state );
    ui->PostMessage( msg );
}


void
center_on_unit(void)	// CONVENIENCE HOOK
{
	ui->PostMessage( UI_CENTER_ON_UNIT );
}



//---------------------------------------------------------------------
// Work functions
// @@@@
#include <Alert.h>

// UI_POPUP_NEWCITY_DIALOG,
// UI_SET_TURN_DONE_BUTTON_STATE,
// UI_CENTER_ON_UNIT,
