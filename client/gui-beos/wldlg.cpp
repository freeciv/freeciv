/********************************************************************** 
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <Message.h>
#include "Defs.hpp"
#include "MainWindow.hpp"

extern "C" {
/* @@@@ walk this for correct inclusions*/
#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "mem.h"
#include "packets.h"
#include "worklist.h"
}
#include "wldlg.hpp"

extern struct connection aconnection;

void
popup_worklists_dialog(struct player *pplay)	// CONVENIENCE HOOK
{
    BMessage *msg = new BMessage( UI_POPUP_WORKLISTS_DIALOG );
    msg->AddPointer( "player", pplay );
    ui->PostMessage( msg );
}

void
update_worklist_report_dialog(void)	// HOOK
{
    ui->PostMessage( UI_UPDATE_WORKLIST_REPORT_DIALOG );
}


//---------------------------------------------------------------------
// Work functions
// @@@@
#include <Alert.h>

// UI_POPUP_WORKLISTS_DIALOG,
// UI_UPDATE_WORKLIST_REPORT_DIALOG, 
