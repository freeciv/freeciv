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
#ifndef FC__INPUTDLG_H
#define FC__INPUTDLG_H

#ifndef INTUITION_CLASSUSR_H
#include <intuition/classusr.h>
#endif

struct input_dialog_data
{
	Object *wnd;
	Object *name;
	APTR data;
};

Object *input_dialog_create( Object *parent, char *title, char *text, char *postinputtext,
			     void *ok_callback, APTR ok_data, 
			     void *cancel_callback, APTR cancel_data);
char *input_dialog_get_input(Object *);
void input_dialog_destroy( Object *);

#endif  /* FC__INPUTDLG_H */
