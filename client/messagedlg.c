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

unsigned int messages_where[E_LAST];

char *message_text[E_LAST]={
  "Low Funds                ", 		/* E_LOW_ON_FUNDS */
  "Pollution                ",
  "Global Warming           ",
  "Civil Disorder           ",
  "City Celebrating         ",
  "City Normal              ",
  "City Growth              ",
  "City Needs Aqueduct      ",
  "Famine in City           ",
  "City Captured/Destroyed  ",
  "Building Unavailable Item",
  "Wonder Started           ",
  "Wonder Finished          ",
  "Improvement Built        ",
  "New Improvement Selected ",
  "Forced Improvement Sale  ",
  "Production Upgraded      ",
  "Unit Built               ",
  "Unit Defender Destroyed  ",
  "Unit Defender Survived   ",
  "Collapse to Anarchy      ",
  "Diplomat Actions - Enemy ",
  "Tech from Great Library  ",
  "Player Destroyed         ",		/* E_DESTROYED */
  "Improvement Bought       ",		/* E_IMP_BUY */
  "Improvement Sold         ",		/* E_IMP_SOLD */
  "Unit Bought              ",		/* E_UNIT_BUY */
  "Wonder Stopped           ",		/* E_WONDER_STOPPED */
  "City Needs Aq Being Built",	        /* E_CITY_AQ_BUILDING */
  "Diplomat Actions - Own   ",          /* E_MY_DIPLOMAT */
  "Unit Attack Failed       ",          /* E_UNIT_LOST_ATT */
  "Unit Attack Succeeded    ",          /* E_UNIT_WIN_ATT */
  "Suggest Growth Throttling",          /* E_CITY_GRAN_THROTTLE */
};


/****************************************************************
... 
*****************************************************************/
void init_messages_where(void)
{
  int out_only[] = {E_IMP_BUY, E_IMP_SOLD, E_UNIT_BUY, E_MY_DIPLOMAT,
		    E_UNIT_LOST_ATT, E_UNIT_WIN_ATT};
  int i;

  for(i=0; i<E_LAST; i++) {
    messages_where[i] = MW_OUTPUT | MW_MESSAGES;
  }
  for(i=0; i<sizeof(out_only)/sizeof(int); i++) {
    messages_where[out_only[i]] = MW_OUTPUT;
  }
}

/*************************************************************************/
Widget create_messageopt_dialog(void);
void messageopt_ok_command_callback(Widget w, XtPointer client_data, 
			               XtPointer call_data);
void messageopt_cancel_command_callback(Widget w, XtPointer client_data, 
					XtPointer call_data);
static Widget messageopt_toggles[E_LAST][NUM_MW];

/**************************************************************************
Comparison function for qsort; i1 and i2 are pointers to integers which
index message_text[].
**************************************************************************/
int compar_message_texts(const void *i1, const void *i2)
{
  int j1 = *(const int*)i1;
  int j2 = *(const int*)i2;
  
  return strcmp(message_text[j1], message_text[j2]);
}

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
  int sorted[E_LAST];
  
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
    sorted[i] = i;
  }
  qsort(sorted, E_LAST, sizeof(int), compar_message_texts);
  
  for(i=0;i<E_LAST;i++)  {
    int top_line = (!i || i==E_LAST/2);
    int is_col1 = i<E_LAST/2;
    label = XtVaCreateManagedWidget("label",
				    labelWidgetClass,
				    is_col1?col1:col2,
				    XtNlabel, message_text[sorted[i]],
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
      messageopt_toggles[sorted[i]][j]=toggle;
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

