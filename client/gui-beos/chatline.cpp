/* chatline.cpp */

#include <Message.h>
#include "Defs.hpp"
#include "MainWindow.hpp"
#include "chatline.h"

void
real_append_output_window(const char *astring)		// HOOK
{
	BMessage *msg = new BMessage( UI_APPEND_OUTPUT_WINDOW );
	msg->AddString( "string", astring );
	ui->PostMessage(msg);
}


void
log_output_window(void)		// HOOK
{
	ui->PostMessage( UI_LOG_OUTPUT_WINDOW );
}


void
clear_output_window(void)		// HOOK
{
	ui->PostMessage( UI_CLEAR_OUTPUT_WINDOW );
}
