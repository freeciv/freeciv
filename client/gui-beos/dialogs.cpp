/* dialogs.cpp */

#include <Message.h>
#include "Defs.hpp"
#include "MainWindow.hpp"
#include "dialogs.h"
#include "BdhDialog.h"


// ----------------------------------------------

void
popup_notify_goto_dialog(char *headline, char *lines, int x, int y)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_NOTIFY_GOTO_DIALOG );
    msg->AddString( "headline", headline );
    msg->AddString( "lines", lines );
	msg->AddInt32( "x", x );
	msg->AddInt32( "y", y );
    ui->PostMessage( msg );
}


// ----------------------------------------------

void
popup_notify_dialog(char *caption, char *headline, char *lines)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_NOTIFY_DIALOG );
    msg->AddString( "caption", caption );
    msg->AddString( "headline", headline );
    msg->AddString( "lines", lines );
    ui->PostMessage( msg );
}


// ----------------------------------------------

BdhDialog *races_dialog = NULL;

void
popup_races_dialog(void)	// HOOK
{
	ui->PostMessage( UI_POPUP_RACES_DIALOG );
}


void
popdown_races_dialog(void)	// HOOK
{
	if (!races_dialog) return;
	ui->PostMessage( UI_POPDOWN_RACES_DIALOG );
}


// ----------------------------------------------

void
popup_unit_select_dialog(struct tile *ptile)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_UNIT_SELECT_DIALOG );
    msg->AddPointer( "tile", ptile );
    ui->PostMessage( msg );
}


void
races_toggles_set_sensitive(int bits1, int bits2)	// HOOK
{
    BMessage *msg = new BMessage( UI_RACES_TOGGLES_SET_SENSITIVE_DIALOG );
    msg->AddInt32( "bits", bits1 );
    msg->AddInt32( "morebits", bits2 );
    ui->PostMessage( msg );
}


// ----------------------------------------------

void
popup_revolution_dialog(void)	// HOOK
{
	ui->PostMessage( UI_POPUP_REVOLUTION_DIALOG );
}


// ----------------------------------------------

void
popup_government_dialog(void)	// HOOK
{
	ui->PostMessage( UI_POPUP_GOVERNMENT_DIALOG );
}


// ----------------------------------------------

BdhDialog *caravan_dialog = NULL;

// Caravan has reached destination.  Query on what to do now.
void
popup_caravan_dialog(struct unit *punit,	// EXTERNAL HOOK
                     struct city *phomecity, struct city *pdestcity)
{
    BMessage *msg = new BMessage( UI_POPUP_CARAVAN_DIALOG );
    msg->AddPointer( "unit", punit );
    msg->AddPointer( "home", phomecity );
    msg->AddPointer( "dest", pdestcity );
    ui->PostMessage( msg );
}


int
caravan_dialog_is_open(void)	// EXTERNAL HOOK
{
	return !!caravan_dialog;
}


// ----------------------------------------------

BdhDialog *diplomat_dialog = NULL;

void
popup_diplomat_dialog(struct unit *punit, int dest_x, int dest_y)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_DIPLOMAT_DIALOG );
    msg->AddPointer( "unit", punit );
	msg->AddInt32( "x", dest_x );
    msg->AddInt32( "y", dest_y );
    ui->PostMessage( msg );
}


int
diplomat_dialog_is_open(void)	// HOOK
{
	return !!diplomat_dialog;
}


void
popup_incite_dialog(struct city *pcity)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_INCITE_DIALOG );
    msg->AddPointer( "city", pcity );
    ui->PostMessage( msg );
}


void
popup_bribe_dialog(struct unit *punit)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_BRIBE_DIALOG );
    msg->AddPointer( "unit", punit );
    ui->PostMessage( msg );
}


void
popup_pillage_dialog(struct unit *punit, int may_pillage)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_PILLAGE_DIALOG );
    msg->AddPointer( "unit", punit );
    msg->AddBool( "allowed", !!may_pillage );
    ui->PostMessage( msg );
}


void
popup_sabotage_dialog(struct city *pcity)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_SABOTAGE_DIALOG );
    msg->AddPointer( "city", pcity );
    ui->PostMessage( msg );
}


// ----------------------------------------------

void
popup_unit_connect_dialog(struct unit *punit, int dest_x, int dest_y)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_UNIT_CONNECT_DIALOG );
    msg->AddPointer( "unit", punit );
	msg->AddInt32( "x", dest_x );
    msg->AddInt32( "y", dest_y );
    ui->PostMessage( msg );
}


//---------------------------------------------------------------------
// Work functions
// @@@@
#include <Alert.h>

// UI_POPUP_NOTIFY_GOTO_DIALOG,
// UI_POPUP_NOTIFY_DIALOG,
// UI_POPUP_RACES_DIALOG,
// UI_POPDOWN_RACES_DIALOG,
// UI_POPUP_UNIT_SELECT_DIALOG,
// UI_RACES_TOGGLES_SET_SENSITIVE_DIALOG,
// UI_POPUP_REVOLUTION_DIALOG,
// UI_POPUP_GOVERNMENT_DIALOG,
// UI_POPUP_CARAVAN_DIALOG,
// UI_POPUP_DIPLOMAT_DIALOG,
// UI_POPUP_INCITE_DIALOG,
// UI_POPUP_BRIBE_DIALOG,
// UI_POPUP_PILLAGE_DIALOG,
// BACKEND_PROCESS_CARAVAN_ARRIVAL,
// UI_POPUP_SABOTAGE_DIALOG,
// UI_POPUP_UNIT_CONNECT_DIALOG,

