/* messagewin.c -- PLACEHOLDER */

#include <Message.h>
#include "Defs.hpp"
#include "MainWindow.hpp"
#include "messagewin.h"

void
popup_meswin_dialog(void)	// HOOK
{
	ui->PostMessage( UI_POPUP_MESWIN_DIALOG );
}

bool is_meswin_open(void)
{
  // PORTME
}

void
real_update_meswin_dialog(void)	// HOOK
{
	ui->PostMessage( UI_UPDATE_MESWIN_DIALOG );
}


void
real_clear_notify_window(void)	// HOOK
{
	ui->PostMessage( UI_CLEAR_NOTIFY_WINDOW );
}


void
real_add_notify_window(struct packet_generic_message *packet)	// HOOK
{
    BMessage *msg = new BMessage( UI_ADD_NOTIFY_WINDOW );
    msg->AddPointer( "packet", packet );
    ui->PostMessage( msg );
}

//---------------------------------------------------------------------
// Work functions
// @@@@
#include <Alert.h>

//  UI_POPUP_MESWIN_DIALOG,
//  UI_UPDATE_MESWIN_DIALOG,
//  UI_CLEAR_NOTIFY_WINDOW,
//  UI_ADD_NOTIFY_WINDOW,
//  UI_MESWIN_UPDATE_DELAY,
