/* diplodlg.cpp */

#include <Message.h>
#include "MainWindow.hpp"
#include "Defs.hpp"
#include "diplodlg.h"

void
handle_diplomacy_accept_treaty(struct packet_diplomacy_info *pa)	// HOOK
{
    BMessage *msg = new BMessage( UI_HANDLE_DIPLO_ACCEPT_TREATY );
    msg->AddPointer( "packet", pa );
    ui->PostMessage( msg );
}


void
handle_diplomacy_init_meeting(struct packet_diplomacy_info *pa)	// HOOK
{
    BMessage *msg = new BMessage( UI_HANDLE_DIPLO_INIT_MEETING );
    msg->AddPointer( "packet", pa );
    ui->PostMessage( msg );
}


void
handle_diplomacy_create_clause(struct packet_diplomacy_info *pa)	// HOOK
{
    BMessage *msg = new BMessage( UI_HANDLE_DIPLO_CREATE_CLAUSE );
    msg->AddPointer( "packet", pa );
    ui->PostMessage( msg );
}


void
handle_diplomacy_cancel_meeting(struct packet_diplomacy_info *pa)	// HOOK
{
    BMessage *msg = new BMessage( UI_HANDLE_DIPLO_CANCEL_MEETING );
    msg->AddPointer( "packet", pa );
    ui->PostMessage( msg );
}


void
handle_diplomacy_remove_clause(struct packet_diplomacy_info *pa)	// HOOK
{
    BMessage *msg = new BMessage( UI_HANDLE_DIPLO_REMOVE_CLAUSE );
    msg->AddPointer( "packet", pa );
    ui->PostMessage( msg );
}

 


//---------------------------------------------------------------------
// Work functions
// @@@@
#include <Alert.h>

// UI_HANDLE_DIPLO_ACCEPT_TREATY,
// UI_HANDLE_DIPLO_INIT_MEETING,
// UI_HANDLE_DIPLO_CREATE_CLAUSE,
// UI_HANDLE_DIPLO_CANCEL_MEETING,
// UI_HANDLE_DIPLO_REMOVE_CLAUSE,
