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

#include "clinet.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "optiondlg.h"
#include "options.h"

#include "messagedlg.h"

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
		    XtNlabel, state? _("Yes") : _("No"), NULL);
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
  Widget shell,form,title,explanation,ok,cancel,col[2];
  Widget colhead[2], space_head[2];
  Widget label[E_LAST];
  Widget longest_label[2] = { 0, 0 };
  Widget toggle=0;
  int i, j, len, longest_len = 0;
  
  shell = I_T(XtCreatePopupShell("messageoptpopup",
				 transientShellWidgetClass,
				 toplevel, NULL, 0));

  form = XtVaCreateManagedWidget("messageoptform", 
				 formWidgetClass, 
				 shell, NULL);   

  title = I_L(XtVaCreateManagedWidget("messageopttitle",
				      labelWidgetClass,
				      form, NULL));

  explanation = I_L(XtVaCreateManagedWidget("messageoptexpl",
					    labelWidgetClass,
					    form, NULL));

  col[0] = XtVaCreateManagedWidget("messageoptcol1",
  				 formWidgetClass,
				 form, NULL);

  col[1] = XtVaCreateManagedWidget("messageoptcol2",
  				 formWidgetClass,
				 form, NULL);

  for(i=0; i<2; i++) {
    /* space_head labels are "empty" labels in column heading which are
     * used so that we can arrange the constraints without loops.
     * They essentially act as vertical filler.
     */
    space_head[i] = XtVaCreateManagedWidget("messageoptspacehead",
					    labelWidgetClass,
					    col[i], NULL);
  
    colhead[i] = I_L(XtVaCreateManagedWidget("messageoptcolhead",
					     labelWidgetClass,
					     col[i], NULL));
  }

  for(i=0;i<E_LAST;i++)  {
    int top_line = (!i || i==E_LAST/2);
    int icol = (i>=E_LAST/2);
    const char *text = _(message_text[sorted_events[i]]);
    label[i] = XtVaCreateManagedWidget("label",
				       labelWidgetClass,
				       col[icol],
				       XtNlabel, text,
				       XtNfromVert,
				       top_line?space_head[icol]:label[i-1],
				       NULL);
    len = strlen(text);
    if (top_line || len > longest_len) {
      longest_len = len;
      longest_label[icol] = label[i];
    }
  }

  /* Align the column headings: */
  for(i=0; i<2; i++) {
    XtVaSetValues(colhead[i], XtNfromHoriz, longest_label[i], NULL);
  }

  for(i=0;i<E_LAST;i++)  {
    int top_line = (!i || i==E_LAST/2);
    int icol = (i>=E_LAST/2);
    Widget vert = top_line?space_head[icol]:label[i-1];
    for(j=0; j<NUM_MW; j++) {
      toggle = XtVaCreateManagedWidget("toggle",
				       toggleWidgetClass,
				       col[icol],
				       XtNfromHoriz,
				       (j==0?longest_label[icol]:toggle),
				       XtNfromVert, vert,
				       NULL);
      XtAddCallback(toggle, XtNcallback, toggle_callback, NULL);
      messageopt_toggles[sorted_events[i]][j]=toggle;
    }
  }

  ok = I_L(XtVaCreateManagedWidget("messageoptokcommand",
				   commandWidgetClass,
				   form, NULL));
  
  cancel = I_L(XtVaCreateManagedWidget("messageoptcancelcommand",
				       commandWidgetClass,
				       form, NULL));
	       
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

