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
#include <fc_config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "packets.h"
#include "support.h"
#include "version.h"

/* client */
#include "client_main.h"
#include "chatline.h"
#include "colors.h"
#include "connectdlg_common.h"
#include "dialogs.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "options.h"
#include "packhand.h"
#include "tilespec.h"

#include "connectdlg.h"


/**************************************************************************
 really close and destroy the dialog.
**************************************************************************/
void really_close_connection_dialog(void)
{
}

/**************************************************************************
 close and destroy the dialog but only if we don't have a local
 server running (that we started).
**************************************************************************/
void close_connection_dialog() 
{   
  if (!is_server_running()) {
    really_close_connection_dialog();
  }
}

/**************************************************************************
  gtk client does nothing here. This gets called when one is rejected
  from game.
**************************************************************************/
void gui_server_connect(void)
{
}
