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
#include <stdarg.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Scrollbar.h>
#include <X11/Xaw/AsciiText.h>  
#include <X11/Xaw/Toggle.h>     

#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "chatline.h"
#include "cityrep.h"
#include "clinet.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "options.h"

#include "optiondlg.h"

static Widget option_dialog_shell;

/******************************************************************/
void create_option_dialog(void);

void option_ok_command_callback(Widget w, XtPointer client_data, 
			        XtPointer call_data);
void option_cancel_command_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data);

/****************************************************************
... 
*****************************************************************/
void popup_option_dialog(void)
{
  client_option *o;
  char valstr[64];

  create_option_dialog();

  for (o=options; o->name; ++o) {
    switch (o->type) {
    case COT_BOOL:
      XtVaSetValues((Widget) o->p_gui_data, XtNstate, *(o->p_bool_value),
		    XtNlabel, *(o->p_bool_value) ? _("Yes") : _("No"), NULL);
      break;
    case COT_INT:
      my_snprintf(valstr, sizeof(valstr), "%d", *(o->p_int_value));
      XtVaSetValues((Widget) o->p_gui_data, XtNstring, valstr, NULL);
      break;
    }
  }

  xaw_set_relative_position(toplevel, option_dialog_shell, 25, 25);
  XtPopup(option_dialog_shell, XtGrabNone);
  XtSetSensitive(main_form, FALSE);
}




/****************************************************************
...
*****************************************************************/
void create_option_dialog(void)
{
  Widget option_form, option_label;
  Widget option_ok_command, option_cancel_command;
  Widget prev_widget, longest_label = 0;
  client_option *o;
  size_t longest_len = 0;
  
  option_dialog_shell =
    I_T(XtCreatePopupShell("optionpopup", transientShellWidgetClass,
			   toplevel, NULL, 0));

  option_form = XtVaCreateManagedWidget("optionform", 
				        formWidgetClass, 
				        option_dialog_shell, NULL);   

  option_label =
    I_L(XtVaCreateManagedWidget("optionlabel", labelWidgetClass, 
				option_form, NULL));

  prev_widget = option_label; /* init the prev-Widget */
  for (o = options; o->name; o++) {
    const char *descr = _(o->description);
    size_t len = strlen(descr);

    /* 
     * Remember widget so we can reset the vertical position; need to
     * do this because labels and toggles etc have different heights.
     */
    o->p_gui_data = (void *) prev_widget;

    prev_widget = 
      XtVaCreateManagedWidget("label", labelWidgetClass, option_form,
			      XtNlabel, descr,
			      XtNfromVert, prev_widget,
			      NULL);
    if (len > longest_len) {
      longest_len = len;
      longest_label = prev_widget;
    }
  }

  for (o = options; o->name; o++) {
    /* 
     * At the start of the loop o->p_gui_data will contain the widget
     * which is above the label widget which is associated with this
     * option.
     */
    switch (o->type) {
    case COT_BOOL:
      prev_widget =
	XtVaCreateManagedWidget("toggle", toggleWidgetClass, option_form,
				XtNfromHoriz, longest_label,
				XtNfromVert, o->p_gui_data,
				NULL);
      XtAddCallback(prev_widget, XtNcallback, toggle_callback, NULL);
      break;
    case COT_INT:
      prev_widget =
	XtVaCreateManagedWidget("input", asciiTextWidgetClass, option_form,
				XtNfromHoriz, longest_label,
				XtNfromVert, o->p_gui_data,
				NULL);
      break;
    }

    /* store the final widget */
    o->p_gui_data = (void *) prev_widget;
  }

  option_ok_command =
    I_L(XtVaCreateManagedWidget("optionokcommand", commandWidgetClass,
				option_form, XtNfromVert, prev_widget,
				NULL));
  
  option_cancel_command =
    I_L(XtVaCreateManagedWidget("optioncancelcommand", commandWidgetClass,
				option_form, XtNfromVert, prev_widget,
				NULL));
	
  XtAddCallback(option_ok_command, XtNcallback, 
		option_ok_command_callback, NULL);
  XtAddCallback(option_cancel_command, XtNcallback, 
		option_cancel_command_callback, NULL);

  XtRealizeWidget(option_dialog_shell);

  xaw_horiz_center(option_label);
}


/**************************************************************************
 Changes the label of the toggle widget to Yes/No depending on the state of
 the toggle.
**************************************************************************/
void toggle_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  Boolean b;

  XtVaGetValues(w, XtNstate, &b, NULL);
  XtVaSetValues(w, XtNlabel, b ? _("Yes") : _("No"), NULL);
}

/**************************************************************************
...
**************************************************************************/
void option_ok_command_callback(Widget w, XtPointer client_data, 
			       XtPointer call_data)
{
  Boolean b;
  client_option *o;
  XtPointer dp;

  for (o=options; o->name; ++o) {
    switch (o->type) {
    case COT_BOOL:
      XtVaGetValues((Widget) o->p_gui_data, XtNstate, &b, NULL);
      *(o->p_bool_value) = b;
      break;
    case COT_INT:
      XtVaGetValues(o->p_gui_data, XtNstring, &dp, NULL);
      sscanf(dp, "%d", o->p_int_value);
      break;
    }
  }

  XtSetSensitive(main_form, TRUE);
  XtDestroyWidget(option_dialog_shell);
}

/**************************************************************************
...
**************************************************************************/
void option_cancel_command_callback(Widget w, XtPointer client_data, 
				    XtPointer call_data)
{
  XtSetSensitive(main_form, TRUE);
  XtDestroyWidget(option_dialog_shell);
}
