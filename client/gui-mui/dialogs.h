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
#ifndef FC__DIALOGS_H
#define FC__DIALOGS_H

#include "dialogs_g.h"

#ifndef INTUITION_CLASSUSR_H
#include <intuition/classusr.h>
#endif

struct New_Msg_Dlg
{
	STRPTR label;
	APTR function;
	APTR data;
};

struct popup_message_data
{
	Object *wnd;
	APTR data;
};

void message_close( struct popup_message_data *msg);

Object *popup_message_dialog_args( Object *parent, char *title, char *text, struct New_Msg_Dlg * );
Object *popup_message_dialog( Object *parent, char *title, char *text, ... );
void destroy_message_dialog( Object *);

void popup_upgrade_dialog(struct unit *punit);

#endif  /* FC__DIALOGS_H */
