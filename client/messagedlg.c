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
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Scrollbar.h>
#include <X11/Xaw/Toggle.h>     

#include <game.h>
#include <player.h>
#include <mapview.h>
#include <messagedlg.h>
#include <shared.h>
#include <packets.h>
#include <xstuff.h>
#include <events.h>

extern Widget toplevel, main_form;

extern struct connection aconnection;
extern Display	*display;

extern int message_values[E_LAST];


/******************************************************************/
Widget messageopt_dialog_shell;
Widget messageopt_lof_toggle;
Widget messageopt_po_toggle;
Widget messageopt_cd_toggle;

/******************************************************************/
void create_messageopt_dialog(void);

void messageopt_ok_command_callback(Widget w, XtPointer client_data, 
			        XtPointer call_data);

/****************************************************************
... 
*****************************************************************/
void popup_messageopt_dialog(void)
{
  create_messageopt_dialog();
  XtVaSetValues(messageopt_lof_toggle, XtNstate, message_values[E_LOW_ON_FUNDS], NULL);
  XtVaSetValues(messageopt_po_toggle, XtNstate, message_values[E_POLLUTION], NULL);
  XtVaSetValues(messageopt_cd_toggle, XtNstate, message_values[E_CITY_DISORDER], NULL);
  
  xaw_set_relative_position(toplevel, messageopt_dialog_shell, 25, 25);
  XtPopup(messageopt_dialog_shell, XtGrabNone);
  XtSetSensitive(main_form, FALSE);
}

/****************************************************************
...
*****************************************************************/
void create_messageopt_dialog(void)
{
  Widget messageopt_form, messageopt_label;
  Widget messageopt_ok_command;
  
  messageopt_dialog_shell = XtCreatePopupShell("messageoptpopup", 
					  transientShellWidgetClass,
					  toplevel, NULL, 0);

  messageopt_form = XtVaCreateManagedWidget("messageoptform", 
				        formWidgetClass, 
				        messageopt_dialog_shell, NULL);   

  messageopt_label = XtVaCreateManagedWidget("messageoptlabel", 
					 labelWidgetClass, 
					 messageopt_form, NULL);   
  
  XtVaCreateManagedWidget("messageoptloflabel", 
			  labelWidgetClass, 
			  messageopt_form, NULL);
  messageopt_lof_toggle = XtVaCreateManagedWidget("messageoptloftoggle", 
					     toggleWidgetClass, 
					     messageopt_form,
					     NULL);
  
  XtVaCreateManagedWidget("messageoptpolabel", 
			  labelWidgetClass, 
			  messageopt_form, NULL);
  messageopt_po_toggle = XtVaCreateManagedWidget("messageoptpotoggle", 
					       toggleWidgetClass, 
					       messageopt_form,
					       NULL);
  
  
  XtVaCreateManagedWidget("messageoptcdlabel", 
			  labelWidgetClass, 
			  messageopt_form, NULL);
  messageopt_cd_toggle = XtVaCreateManagedWidget("messageoptcdtoggle", 
						toggleWidgetClass, 
						messageopt_form,
						NULL);
  
  messageopt_ok_command = XtVaCreateManagedWidget("messageoptokcommand", 
					      commandWidgetClass,
					      messageopt_form,
					      NULL);
  
  XtAddCallback(messageopt_ok_command, XtNcallback, 
		messageopt_ok_command_callback, NULL);

  XtRealizeWidget(messageopt_dialog_shell);

  xaw_horiz_center(messageopt_label);
}

/**************************************************************************
...
**************************************************************************/
void messageopt_ok_command_callback(Widget w, XtPointer client_data, 
			       XtPointer call_data)
{
  Boolean b;
  
  XtSetSensitive(main_form, TRUE);
  XtDestroyWidget(messageopt_dialog_shell);

  XtVaGetValues(messageopt_lof_toggle, XtNstate, &b, NULL);
  message_values[E_LOW_ON_FUNDS]=b;
  XtVaGetValues(messageopt_po_toggle, XtNstate, &b, NULL);
  message_values[E_POLLUTION]=b;
  XtVaGetValues(messageopt_cd_toggle, XtNstate, &b, NULL);
  message_values[E_CITY_DISORDER]=b;
}
