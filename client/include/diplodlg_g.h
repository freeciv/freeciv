/***********************************************************************
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
#ifndef FC__DIPLODLG_G_H
#define FC__DIPLODLG_G_H

/* utility */
#include "shared.h"

/* common */
#include "diptreaty.h"

#include "gui_proto_constructor.h"

GUI_FUNC_PROTO(void, gui_init_meeting,
               struct treaty *ptreaty, struct player *they,
               struct player *initiator)
GUI_FUNC_PROTO(void, gui_recv_cancel_meeting,
               struct treaty *ptreaty, struct player *they,
               struct player *initiator)
GUI_FUNC_PROTO(void, gui_prepare_clause_updt,
               struct treaty *ptreaty, struct player *they)
GUI_FUNC_PROTO(void, gui_recv_create_clause,
               struct treaty *ptreaty, struct player *they)
GUI_FUNC_PROTO(void, gui_recv_remove_clause,
               struct treaty *ptreaty, struct player *they)
GUI_FUNC_PROTO(void, gui_recv_accept_treaty, struct treaty *ptreaty,
               struct player *they)

GUI_FUNC_PROTO(void, close_all_diplomacy_dialogs, void)

#endif /* FC__DIPLODLG_G_H */
