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

#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "packets.h"
#include "player.h"
#include "shared.h"

#include "gui_stuff.h"
#include "mapview.h"
#include "optiondlg.h"
#include "options.h"

#include "messagedlg.h"

extern Widget toplevel, main_form;

extern struct connection aconnection;
extern Display	*display;

/*************************************************************************/
Widget create_messageopt_dialog(void);
void messageopt_ok_command_callback(Widget w, XtPointer client_data, 
			               XtPointer call_data);
void messageopt_cancel_command_callback(Widget w, XtPointer client_data, 
					XtPointer call_data);
static Widget messageopt_toggles[E_LAST][NUM_MW];

/**************************************************************************
... 
**************************************************************************/
void popup_messageopt_dialog(void)
{
  Widget shell;
  int i, j, state;

  shell=create_messageopt_dialog();

  /* Doing this here makes the "No"'s centered consistently */
  for(i=0; i<E_LAST; i++) {
    for(j=0; j<NUM_MW; j++) {
      state = messages_where[i] & (1<<j);
      XtVaSetValues(messageopt_toggles[i][j],
		    XtNstate, state,
		    XtNlabel, state?"Yes":"No", NULL);
    }
  }
  
  xaw_set_relative_position(toplevel, shell, 15, 0);
  XtPopup(shell, XtGrabNone);
  XtSetSensitive(main_form, FALSE);
}

/**************************************************************************
...
**************************************************************************/
Widget create_messageopt_dialog(void)
{
  Widget shell,form,title,explanation,ok,cancel,col1,col2;
  Widget colhead1, colhead2;
  Widget label,last_label=0;
  Widget toggle=0;
  int i, j;
  
  shell = XtCreatePopupShell("messageoptpopup",
			     transientShellWidgetClass,
			     toplevel, NULL, 0);

  form = XtVaCreateManagedWidget("messageoptform", 
				 formWidgetClass, 
				 shell, NULL);   

  title = XtVaCreateManagedWidget("messageopttitle",
  				  labelWidgetClass,
				  form, NULL);

  explanation = XtVaCreateManagedWidget("messageoptexpl",
  				  labelWidgetClass,
				  form, NULL);

  col1 = XtVaCreateManagedWidget("messageoptcol1",
  				 formWidgetClass,
				 form, NULL);

  col2 = XtVaCreateManagedWidget("messageoptcol2",
  				 formWidgetClass,
				 form, NULL);
  
  colhead1 = XtVaCreateManagedWidget("messageoptcolhead",
				     labelWidgetClass,
				     col1, NULL);

  colhead2 = XtVaCreateManagedWidget("messageoptcolhead",
				     labelWidgetClass,
				     col2, NULL);

  for(i=0;i<E_LAST;i++)  {
    int top_line = (!i || i==E_LAST/2);
    int is_col1 = i<E_LAST/2;
    label = XtVaCreateManagedWidget("label",
				    labelWidgetClass,
				    is_col1?col1:col2,
				    XtNlabel, _(message_text[sorted_events[i]]),
				    XtNfromVert, top_line?
				    is_col1?colhead1:colhead2:last_label,
				    NULL);
    for(j=0; j<NUM_MW; j++) {
      toggle = XtVaCreateManagedWidget("toggle",
				       toggleWidgetClass,
				       is_col1?col1:col2,
				       XtNfromHoriz, (j==0?label:toggle),
				       XtNfromVert, top_line?
				       is_col1?colhead1:colhead2:last_label,
				       NULL);
      XtAddCallback(toggle, XtNcallback, toggle_callback, NULL);
      messageopt_toggles[sorted_events[i]][j]=toggle;
    }

    last_label=label; 
  }

  ok = XtVaCreateManagedWidget("messageoptokcommand",
			       commandWidgetClass,
			       form,
			       NULL);
  cancel = XtVaCreateManagedWidget("messageoptcancelcommand",
				   commandWidgetClass,
				   form,
				   NULL);
  XtAddCallback(ok, XtNcallback, messageopt_ok_command_callback, 
                (XtPointer)shell);
  XtAddCallback(cancel, XtNcallback, messageopt_cancel_command_callback, 
                (XtPointer)shell);
  
  XtRealizeWidget(shell);

  xaw_horiz_center(title);
  xaw_horiz_center(explanation);

  return shell;
}

/**************************************************************************
...
**************************************************************************/
void messageopt_cancel_command_callback(Widget w, XtPointer client_data, 
					XtPointer call_data)
{
  XtSetSensitive(main_form, TRUE);
  XtDestroyWidget((Widget)client_data);
}

/**************************************************************************
...
**************************************************************************/
void messageopt_ok_command_callback(Widget w, XtPointer client_data, 
			               XtPointer call_data)
{
  int i, j;
  Boolean b;
  
  XtSetSensitive(main_form, TRUE);

  for(i=0;i<E_LAST;i++)  {
    messages_where[i] = 0;
    for(j=0; j<NUM_MW; j++) {
      XtVaGetValues(messageopt_toggles[i][j], XtNstate, &b, NULL);
      if (b) messages_where[i] |= (1<<j);
    }
  }

  XtDestroyWidget((Widget)client_data);
}

