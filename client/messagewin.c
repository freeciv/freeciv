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

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/List.h>

#include <game.h>
#include <player.h>
#include <meswindlg.h>
#include <diplodlg.h>
#include <inteldlg.h>
#include <packets.h>
#include <clinet.h>
#include <chatline.h>
#include <xstuff.h>
#include <packets.h>
#include <events.h>
#include <dialogs.h>
#include <mapview.h>

extern Widget toplevel, main_form;
extern struct player_race races[];
extern int ai_popup_windows;

Widget meswin_dialog_shell;
Widget meswin_form;
Widget meswin_label;
Widget meswin_list;
Widget meswin_close_command;
Widget meswin_int_command;
Widget meswin_meet_command;

void create_meswin_dialog(void);
void meswin_button_callback(Widget w, XtPointer client_data, 
			      XtPointer call_data);
void meswin_meet_callback(Widget w, XtPointer client_data, 
			   XtPointer call_data);
void meswin_intel_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data);
void meswin_list_callback(Widget w, XtPointer client_data, 
			   XtPointer call_data);


/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_meswin_dialog(void)
{
  if(!meswin_dialog_shell)
    create_meswin_dialog();

  xaw_set_relative_position(toplevel, meswin_dialog_shell, 25, 25);
  XtPopup(meswin_dialog_shell, XtGrabNone);
  update_meswin_dialog();
}


/****************************************************************
...
*****************************************************************/
void create_meswin_dialog(void)
{
  meswin_dialog_shell = XtCreatePopupShell("meswinpopup", 
					  topLevelShellWidgetClass,
					  toplevel, NULL, 0);

  meswin_form = XtVaCreateManagedWidget("meswinform", 
				       formWidgetClass, 
				       meswin_dialog_shell, NULL);

  meswin_label=XtVaCreateManagedWidget("meswinlabel", 
					labelWidgetClass, 
					meswin_form, NULL);   
  
   
  meswin_list = XtVaCreateManagedWidget("meswinlist", 
					 listWidgetClass, 
					 meswin_form, 
					 NULL);
					 
  meswin_close_command = XtVaCreateManagedWidget("meswinclosecommand", 
						  commandWidgetClass,
						  meswin_form,
						  NULL);

  meswin_int_command = XtVaCreateManagedWidget("meswingotocommand", 
						commandWidgetClass,
						meswin_form,
                                                XtNsensitive, False,
						NULL);

  XtAddCallback(meswin_list, XtNcallback, meswin_list_callback, 
		NULL);
	       
  XtAddCallback(meswin_close_command, XtNcallback, meswin_button_callback, 
		NULL);

  XtAddCallback(meswin_int_command, XtNcallback, meswin_intel_callback, 
		NULL);
  
  update_meswin_dialog();

  XtRealizeWidget(meswin_dialog_shell);
}

/**************************************************************************
...
**************************************************************************/

int messages_total;
char *string_ptrs[32];
int xpos[32];
int ypos[32];

/**************************************************************************
...
**************************************************************************/

void clear_notify_window()
{
  int i;
  for (i = 0; i <messages_total; i++)
    free(string_ptrs[i]);
  string_ptrs[0]=0;
  messages_total = 0;
  update_meswin_dialog();
}

/**************************************************************************
...
**************************************************************************/
void add_notify_window(struct packet_generic_message *packet)
{
  char *s;
  int nspc;
  
  if (message_values[packet->event]) 
    popup_notify_goto_dialog("Popup Request", packet->message, 
			     packet->x, 
			     packet->y);

  if (messages_total == 32) return; /* should not happend... */ 
  s = (char *)malloc(strlen(packet->message) + 50);
  if (!strncmp(packet->message, "Game: ", 6)) 
   strcpy(s, packet->message + 6);
  else
    strcpy(s, packet->message);

  nspc=50-strlen(s);
  if(nspc>0)
    strncat(s, "                                                  ", nspc);
  
  xpos[messages_total] = packet->x;
  ypos[messages_total] = packet->y;
  string_ptrs[messages_total] = s;
  messages_total++;
  string_ptrs[messages_total] = 0;  
  update_meswin_dialog();
}

/**************************************************************************
...
**************************************************************************/
void update_meswin_dialog(void)
{
  if (!meswin_dialog_shell) { 
    if (messages_total > 0 && 
        (!game.player_ptr->ai.control || ai_popup_windows))
      popup_meswin_dialog();
  }
   if(meswin_dialog_shell) {
     Dimension width;
     if(messages_total==0) {
       string_ptrs[0]="                                                  ";
       XawListChange(meswin_list, string_ptrs, 1, 0, True);
     }
     else
       XawListChange(meswin_list, string_ptrs, messages_total, 0, True);

     XtVaGetValues(meswin_list,  XtNwidth, &width, NULL);
     XtVaSetValues(meswin_label, XtNwidth, width, NULL); 
   }

}

/**************************************************************************
...
**************************************************************************/
void meswin_list_callback(Widget w, XtPointer client_data, 
			   XtPointer call_data)
{
  XawListReturnStruct *ret;

  ret=XawListShowCurrent(meswin_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    if (xpos[ret->list_index] || ypos[ret->list_index]) 
      XtSetSensitive(meswin_int_command, TRUE);
    else
      XtSetSensitive(meswin_int_command, FALSE);
    return;
  }    
  XtSetSensitive(meswin_int_command, FALSE);
}


/**************************************************************************
...
**************************************************************************/
void meswin_button_callback(Widget w, XtPointer client_data, 
			      XtPointer call_data)
{
  XtDestroyWidget(meswin_dialog_shell);
  meswin_dialog_shell=0;
}

/**************************************************************************
...
**************************************************************************/
void meswin_intel_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data)
{
  XawListReturnStruct *ret;

  ret=XawListShowCurrent(meswin_list);

  if(ret->list_index!=XAW_LIST_NONE && (xpos[ret->list_index] != 0 || ypos[ret->list_index]!=0))
    center_tile_mapcanvas(xpos[ret->list_index], ypos[ret->list_index]);
}

