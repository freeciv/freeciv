/* cityrep.cpp */

#include <Message.h>
#include "Defs.hpp"
#include "MainWindow.hpp"
#include "cityrep.h"

void
popup_city_report_dialog(bool make_modal)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_CITY_REPORT_DIALOG );
    msg->AddBool( "modal", !!make_modal );
    ui->PostMessage( msg );
}


void
city_report_dialog_update(void)	// HOOK
{
    BMessage *msg = new BMessage( UI_UPDATE_CITY_REPORT_DIALOG );
    msg->AddString( "all", "all" );
    ui->PostMessage( msg );
}


void
city_report_dialog_update_city(struct city *pcity)	// HOOK
{
    BMessage *msg = new BMessage( UI_UPDATE_CITY_REPORT_DIALOG );
    msg->AddPointer( "city", pcity );
    ui->PostMessage( msg );
}


//---------------------------------------------------------------------
// Work functions
// @@@@
#include <Alert.h>

// UI_POPUP_CITY_REPORT_DIALOG,
// UI_UPDATE_CITY_REPORT_DIALOG,
