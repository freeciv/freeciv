/* repodlgs.cpp */
#include <Alert.h>	// temporary

#include <Message.h>
#include "Defs.hpp"
#include "MainWindow.hpp"
#include "repodlgs.h"

char *
get_report_title(char *report_name)	// HOOK
{
	// @@@@ needs special attention
	NOT_FINISHED( "get_report_title(char" );
	return "";
}


void
update_report_dialogs(void)	// HOOK
{
	ui->PostMessage( UI_UPDATE_REPORT_DIALOGS );
}


void
science_dialog_update(void)	// HOOK
{
	ui->PostMessage( UI_UPDATE_SCIENCE_DIALOG );
}


void
popup_science_dialog(bool make_modal)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_SCIENCE_DIALOG );
    msg->AddBool( "modal", !!make_modal );
    ui->PostMessage( msg );
}


void
trade_report_dialog_update(void)	// HOOK
{
	ui->PostMessage( UI_UPDATE_TRADE_REPORT_DIALOG );
}


void
popup_trade_report_dialog(bool make_modal)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_TRADE_REPORT_DIALOG );
    msg->AddBool( "modal", !!make_modal );
    ui->PostMessage( msg );
}


void
activeunits_report_dialog_update(void)	// HOOK
{
	ui->PostMessage( UI_UPDATE_ACTIVEUNITS_REPORT_DIALOG );
}


void
popup_activeunits_report_dialog(bool make_modal)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_ACTIVEUNITS_REPORT_DIALOG );
    msg->AddBool( "modal", !!make_modal );
    ui->PostMessage( msg );
}



//---------------------------------------------------------------------
// Work functions
// @@@@

// UI_REPORT_UPDATE_DELAY,
// UI_UPDATE_REPORT_DIALOGS,
// UI_UPDATE_SCIENCE_DIALOG,
// UI_POPUP_SCIENCE_DIALOG,
// UI_UPDATE_TRADE_REPORT_DIALOG,
// UI_POPUP_TRADE_REPORT_DIALOG,
// UI_UPDATE_ACTIVEUNITS_REPORT_DIALOG,
// UI_POPUP_ACTIVEUNITS_REPORT_DIALOG,
