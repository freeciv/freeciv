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
#include <optiondlg.h>

extern Widget toplevel, main_form;

extern struct connection aconnection;
extern Display	*display;

extern int message_values[E_LAST];

int message_filter[E_LAST]={
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1};

static char *message_text[E_LAST]={
  "Low Funds                ", 		/* E_LOW_ON_FUNDS */
  "Pollution                ",
  "Global Warming           ",
  "Civil Disorder           ",
  "City Celebrating         ",
  "City Normal              ",
  "City Growth              ",
  "City Needs Aqueducts     ",
  "Famine in City           ",
  "City Captured/Destroyed  ",
  "Building Unavialable Item",
  "Wonder Started           ",
  "Wonder Finished          ",
  "Improvement Built        ",
  "New Improvement Selected ",
  "Forced Improvment Sale   ",
  "Production Upgraded      ",
  "Unit Built               ",
  "Unit Destroyed           ",
  "Unit Wins Battle         ",
  "Collapse to Anarchy      ",
  "Diplomat Actions         ",
  "Tech from Great Library  ",
  "Player Destroyed         "};


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
  XtVaSetValues(messageopt_lof_toggle, XtNstate, message_values[E_LOW_ON_FUNDS], 
  	        XtNlabel, message_values[E_LOW_ON_FUNDS]?"Yes":"No", NULL);
  XtVaSetValues(messageopt_po_toggle, XtNstate, message_values[E_POLLUTION],
  	        XtNlabel, message_values[E_POLLUTION]?"Yes":"No", NULL);
  XtVaSetValues(messageopt_cd_toggle, XtNstate, message_values[E_CITY_DISORDER],
  	        XtNlabel, message_values[E_CITY_DISORDER]?"Yes":"No", NULL);
  
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
  
  XtAddCallback(messageopt_lof_toggle, XtNcallback, toggle_callback, NULL);
  XtAddCallback(messageopt_po_toggle, XtNcallback, toggle_callback, NULL);
  XtAddCallback(messageopt_cd_toggle, XtNcallback, toggle_callback, NULL);
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

/*************************************************************************/
Widget create_messagefilter_dialog(void);
void messagefilter_ok_command_callback(Widget w, XtPointer client_data, 
			               XtPointer call_data);
static Widget messagefilter_toggles[E_LAST];

/**************************************************************************
... 
**************************************************************************/
void popup_messagefilter_dialog(void)
{
  Widget shell;

  shell=create_messagefilter_dialog();
  
  xaw_set_relative_position(toplevel, shell, 25, 0);
  XtPopup(shell, XtGrabNone);
  XtSetSensitive(main_form, FALSE);
}

/**************************************************************************
...
**************************************************************************/
Widget create_messagefilter_dialog(void)
{
  Widget shell,form,title,close,col1,col2;
  Widget label,last_label=0;
  Widget toggle;
  int i;
  
  shell = XtCreatePopupShell("messagefilterpopup",
			     transientShellWidgetClass,
			     toplevel, NULL, 0);

  form = XtVaCreateManagedWidget("messagefilterform", 
				 formWidgetClass, 
				 shell, NULL);   

  title = XtVaCreateManagedWidget("messagefilterlabel",
  				  labelWidgetClass,
				  form, NULL);

  col1 = XtVaCreateManagedWidget("messagefiltercol1",
  				 formWidgetClass,
				 form, NULL);

  col2 = XtVaCreateManagedWidget("messagefiltercol2",
  				 formWidgetClass,
				 form, NULL);

  for(i=0;i<E_LAST;i++)  {
    int top_line = (!i || i==E_LAST/2);
    label = XtVaCreateManagedWidget("label",
				    labelWidgetClass,
				    i<E_LAST/2?col1:col2,
				    XtNlabel, message_text[i],
				    top_line?NULL:XtNfromVert, last_label,
				    NULL);
    toggle = XtVaCreateManagedWidget("toggle",
    				     toggleWidgetClass,
				     i<E_LAST/2?col1:col2,
				     XtNlabel, message_filter[i]?"Yes":"No ",
				     XtNstate, message_filter[i],
				     XtNfromHoriz, label,
				     top_line?NULL:XtNfromVert, last_label,
				     NULL);
    XtAddCallback(toggle, XtNcallback, toggle_callback, NULL);

    messagefilter_toggles[i]=toggle;
    last_label=label; 
  }

  close = XtVaCreateManagedWidget("messagefilterokcommand",
  				  commandWidgetClass,
				  form,
				  NULL);
  XtAddCallback(close, XtNcallback, messagefilter_ok_command_callback, 
                (XtPointer)shell);
  
  XtRealizeWidget(shell);

  xaw_horiz_center(title);

  return shell;
}

/**************************************************************************
...
**************************************************************************/
void messagefilter_ok_command_callback(Widget w, XtPointer client_data, 
			               XtPointer call_data)
{
  int i;
  Boolean b;
  
  XtSetSensitive(main_form, TRUE);

  for(i=0;i<E_LAST;i++)  {
    XtVaGetValues(messagefilter_toggles[i], XtNstate, &b, NULL);
    message_filter[i]=b;
  }

  XtDestroyWidget((Widget)client_data);
}
