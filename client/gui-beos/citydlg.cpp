/* citydlg.cpp */
// Multiple city dialogs can be active at once -- one dialog per city

#include <Message.h>
#include "Defs.hpp"
#include "MainWindow.hpp"
#include "citydlg.h"

void
popup_city_dialog(struct city *pcity, int make_modal)		// HOOK
{
	BMessage *msg = new BMessage( UI_POPUP_CITY_DIALOG );
	msg->AddPointer( "city", pcity );
	msg->AddBool( "modal", !!make_modal );
	ui->PostMessage( msg );
}


void
popdown_city_dialog(struct city *pcity)		// HOOK
{
	BMessage *msg = new BMessage( UI_POPDOWN_CITY_DIALOG );
	msg->AddPointer( "city", pcity );
	ui->PostMessage( msg );
}


void
popdown_all_city_dialogs(void)		// HOOK
{
	BMessage *msg = new BMessage( UI_POPDOWN_CITY_DIALOG );
	msg->AddString( "all", "all" );
	ui->PostMessage( msg );
}


void
refresh_city_dialog(struct city *pcity)		// HOOK
{
	BMessage *msg = new BMessage( UI_REFRESH_CITY_DIALOG );
	msg->AddPointer( "city", pcity );
	ui->PostMessage( msg );
}


void
refresh_unit_city_dialogs(struct unit *punit)		// HOOK
{
	BMessage *msg = new BMessage( UI_REFRESH_UNIT_CITY_DIALOGS );
	msg->AddPointer( "unit", punit );
	ui->PostMessage( msg );
}


//------------------------------------------------------------------------
// Work functions
// @@@@
#include <Alert.h>

//  UI_POPDOWN_CITY_DIALOG,
//  UI_POPUP_CITY_DIALOG,
//  UI_REFRESH_CITY_DIALOG,
//  UI_REFRESH_UNIT_CITY_DIALOGS,
