/***********************************************************************
 Freeciv - Copyright (C) 2003 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__RATESDLG_G_H
#define FC__RATESDLG_G_H

#include "gui_proto_constructor.h"

GUI_FUNC_PROTO(void, popup_rates_dialog, void)

GUI_FUNC_PROTO(void, real_multipliers_dialog_update, void *unused)

/* Actually defined in update_queue.c */
void multipliers_dialog_update(void);

#endif  /* FC__RATESDLG_G_H */
