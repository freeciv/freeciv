/* helpdlg.cpp */

#include <Message.h>
#include "Defs.hpp"
#include "MainWindow.hpp"
#include "helpdlg.hpp"
#include "BdhDialog.h"

BdhDialog *help_dialog = NULL;

void
popup_help_dialog(int item)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_HELP_DIALOG );
    msg->AddInt32( "item", item );
    ui->PostMessage( msg );
}


void
popup_help_dialog_string(char *item)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_HELP_DIALOG_STRING );
    msg->AddString( "item", item );
    ui->PostMessage( msg );
}


void
popup_help_dialog_typed(char *item, enum help_page_type htype)	// HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_HELP_DIALOG_TYPED );
    msg->AddString( "item", item );
	msg->AddInt32( "htype", htype );
    ui->PostMessage( msg );
}


void
popdown_help_dialog(void)	// HOOK
{
	if (!help_dialog) return;
    ui->PostMessage( UI_POPDOWN_HELP_DIALOG );
}




//---------------------------------------------------------------------
// Work functions
// @@@@
#include <Alert.h>

// UI_POPUP_HELP_DIALOG,
// UI_POPUP_HELP_DIALOG_STRING,
// UI_POPUP_HELP_DIALOG_TYPED,
// UI_POPDOWN_HELP_DIALOG,
