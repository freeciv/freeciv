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

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/Viewport.h>

#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"

#include "chatline.h"
#include "citydlg.h"
#include "clinet.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "options.h"

#include "messagewin.h"

extern Widget toplevel;
extern Display *display;
extern Atom wm_delete_window;

Widget meswin_dialog_shell;
Widget meswin_form;
Widget meswin_label;
Widget meswin_list;
Widget meswin_viewport;
Widget meswin_close_command;
Widget meswin_goto_command;
Widget meswin_popcity_command;

void create_meswin_dialog(void);
void meswin_scroll_down(void);
void meswin_close_callback(Widget w, XtPointer client_data, 
			      XtPointer call_data);
void meswin_list_callback(Widget w, XtPointer client_data, 
			   XtPointer call_data);
void meswin_goto_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data);
void meswin_popcity_callback(Widget w, XtPointer client_data, 
			     XtPointer call_data);

static char *dummy_message_list[] = {
  "                                                        ", 0 };

#define N_MSG_VIEW 24 		/* max before scrolling happens */

static int delay_meswin_update=0;

/******************************************************************
 Turn off updating of message window
*******************************************************************/
void meswin_update_delay_on(void)
{
  delay_meswin_update=1;
}

/******************************************************************
 Turn on updating of message window
*******************************************************************/
void meswin_update_delay_off(void)
{
  delay_meswin_update=0;
}


/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_meswin_dialog(void)
{
  int updated = 0;
  
  if(!meswin_dialog_shell) {
    create_meswin_dialog();
    updated = 1;		/* create_ calls update_ */
  }

  xaw_set_relative_position(toplevel, meswin_dialog_shell, 25, 25);
  XtPopup(meswin_dialog_shell, XtGrabNone);
  if(!updated) 
    update_meswin_dialog();
  
  /* Is this necessary here? 
   * from popup_city_report_dialog():
   * force refresh of viewport so the scrollbar is added.
   * Buggy sun athena requires this
   */
  XtVaSetValues(meswin_viewport, XtNforceBars, True, NULL);
  
  meswin_scroll_down();
}


/****************************************************************
...
*****************************************************************/
void create_meswin_dialog(void)
{
  meswin_dialog_shell =
    I_IN(I_T(XtCreatePopupShell("meswinpopup", topLevelShellWidgetClass,
				toplevel, NULL, 0)));

  meswin_form = XtVaCreateManagedWidget("meswinform", 
				       formWidgetClass, 
				       meswin_dialog_shell, NULL);

  meswin_label =
    I_L(XtVaCreateManagedWidget("meswinlabel", labelWidgetClass, 
				meswin_form, NULL));
  
  meswin_viewport = XtVaCreateManagedWidget("meswinviewport", 
					    viewportWidgetClass, 
					    meswin_form, 
					    NULL);
   
  meswin_list = XtVaCreateManagedWidget("meswinlist", 
					listWidgetClass,
					meswin_viewport,
					XtNlist,
					(XtArgVal)dummy_message_list,
					NULL);
					 
  meswin_close_command =
    I_L(XtVaCreateManagedWidget("meswinclosecommand", commandWidgetClass,
				meswin_form, NULL));

  meswin_goto_command =
    I_L(XtVaCreateManagedWidget("meswingotocommand", commandWidgetClass,
				meswin_form,
				XtNsensitive, False,
				NULL));

  meswin_popcity_command =
    I_L(XtVaCreateManagedWidget("meswinpopcitycommand", commandWidgetClass,
				meswin_form,
				XtNsensitive, False,
				NULL));

  XtAddCallback(meswin_list, XtNcallback, meswin_list_callback, 
		NULL);
	       
  XtAddCallback(meswin_close_command, XtNcallback, meswin_close_callback, 
		NULL);

  XtAddCallback(meswin_goto_command, XtNcallback, meswin_goto_callback, 
		NULL);
  
  XtAddCallback(meswin_popcity_command, XtNcallback, meswin_popcity_callback, 
		NULL);
  
  update_meswin_dialog();

  XtRealizeWidget(meswin_dialog_shell);
  
  XSetWMProtocols(display, XtWindow(meswin_dialog_shell), 
		  &wm_delete_window, 1);
  XtOverrideTranslations(meswin_dialog_shell,
      XtParseTranslationTable("<Message>WM_PROTOCOLS: msg-close-messages()"));

}

/**************************************************************************
...
**************************************************************************/

static int messages_total = 0;	/* current total number of message lines */
static int messages_alloc = 0;	/* number allocated for */
static char **string_ptrs = NULL;
static int *xpos = NULL;
static int *ypos = NULL;

/**************************************************************************
 This makes sure that the next two elements in string_ptrs etc are
 allocated for.  Two = one to be able to grow, and one for the sentinel
 in string_ptrs.
 Note update_meswin_dialog should always be called soon after this since
 it contains pointers to the memory we're reallocing here.
**************************************************************************/
static void meswin_allocate(void)
{
  int i;
  
  if (messages_total+2 > messages_alloc) {
    messages_alloc = messages_total + 32;
    string_ptrs = fc_realloc(string_ptrs, messages_alloc*sizeof(char*));
    xpos = fc_realloc(xpos, messages_alloc*sizeof(int));
    ypos = fc_realloc(ypos, messages_alloc*sizeof(int));
    for( i=messages_total; i<messages_alloc; i++ ) {
      string_ptrs[i] = NULL;
      xpos[i] = 0;
      ypos[i] = 0;
    }
  }
}

/**************************************************************************
...
**************************************************************************/

void clear_notify_window(void)
{
  int i;
  meswin_allocate();
  for (i = 0; i <messages_total; i++) {
    free(string_ptrs[i]);
    string_ptrs[i] = NULL;
    xpos[i] = 0;
    ypos[i] = 0;
  }
  string_ptrs[0]=0;
  messages_total = 0;
  update_meswin_dialog();
  if(meswin_dialog_shell) {
    XtSetSensitive(meswin_goto_command, FALSE);
    XtSetSensitive(meswin_popcity_command, FALSE);
  }
}

/**************************************************************************
...
**************************************************************************/
void add_notify_window(struct packet_generic_message *packet)
{
  char *s;
  int nspc;
  char *game_prefix1 = "Game: ";
  char *game_prefix2 = _("Game: ");
  int gp_len1 = strlen(game_prefix1);
  int gp_len2 = strlen(game_prefix2);
  
  meswin_allocate();
  s = fc_malloc(strlen(packet->message) + 50);
  if (!strncmp(packet->message, game_prefix1, gp_len1)) {
    strcpy(s, packet->message + gp_len1);
  } else if(!strncmp(packet->message, game_prefix2, gp_len2)) {
    strcpy(s, packet->message + gp_len2);
  } else {
    strcpy(s, packet->message);
  }

  nspc=50-strlen(s);
  if(nspc>0)
    strncat(s, "                                                  ", nspc);
  
  xpos[messages_total] = packet->x;
  ypos[messages_total] = packet->y;
  string_ptrs[messages_total] = s;
  messages_total++;
  string_ptrs[messages_total] = 0;
  if (!delay_meswin_update) {
    update_meswin_dialog();
    meswin_scroll_down();
  }
}

/**************************************************************************
 This scrolls the messages window down to the bottom.
 NOTE: it seems this must not be called until _after_ meswin_dialog_shell
 is ...? realized, popped up, ... something.
 Its a toss-up whether we _should_ scroll the window down:
 Against: user will likely want to read from the top and scroll down manually.
 For: if we don't scroll down, new messages which appear at the bottom
 (including combat results etc) will be easily missed.
**************************************************************************/
void meswin_scroll_down(void)
{
  Dimension height;
  int pos;
  
  if (!meswin_dialog_shell)
    return;
  if (messages_total <= N_MSG_VIEW)
    return;
  
  XtVaGetValues(meswin_list, XtNheight, &height, NULL);
  pos = (((double)(messages_total-1))/messages_total)*height;
  XawViewportSetCoordinates(meswin_viewport, 0, pos);
}

/**************************************************************************
...
**************************************************************************/
void update_meswin_dialog(void)
{
  if (!meswin_dialog_shell) { 
    if (messages_total > 0 && 
        (!game.player_ptr->ai.control || ai_popup_windows)) {
      popup_meswin_dialog();
      /* Can return here because popup_meswin_dialog will call
       * this very function again.
       */
      return;
    }
  }
  if(meswin_dialog_shell) {
     Dimension height, iheight, width;
     int i;

     XawFormDoLayout(meswin_form, False);

     if(messages_total==0) 
       XawListChange(meswin_list, dummy_message_list, 1, 0, True);
     else 
       XawListChange(meswin_list, string_ptrs, messages_total, 0, True);
     
     /* Much of the following copied from city_report_dialog_update() */
     XtVaGetValues(meswin_list, XtNlongest, &i, NULL);
     width=i+10;
     /* I don't know the proper way to set the width of this viewport widget.
        Someone who knows is more than welcome to fix this */
     XtVaSetValues(meswin_viewport, XtNwidth, width+15, NULL); 
     XtVaSetValues(meswin_label, XtNwidth, width+15, NULL);

     /* Seems have to do this here so we get the correct height below. */
     XawFormDoLayout(meswin_form, True);

     if(messages_total <= N_MSG_VIEW) {
       XtVaGetValues(meswin_list, XtNheight, &height, NULL);
       XtVaSetValues(meswin_viewport, XtNheight, height, NULL);
     } else {
       XtVaGetValues(meswin_list, XtNheight, &height, NULL);
       XtVaGetValues(meswin_list, XtNinternalHeight, &iheight, NULL);
       height -= (iheight*2);
       height /= messages_total;
       height *= N_MSG_VIEW;
       height += (iheight*2);
       XtVaSetValues(meswin_viewport, XtNheight, height, NULL);
     }
   }
}

/**************************************************************************
...
**************************************************************************/
void meswin_list_callback(Widget w, XtPointer client_data, 
			   XtPointer call_data)
{
  XawListReturnStruct *ret;
  int location_ok = 0;
  int city_ok = 0;

  ret=XawListShowCurrent(meswin_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    struct city *pcity;
    int x, y;
    x = xpos[ret->list_index];
    y = ypos[ret->list_index];
    location_ok = (y >= 0 && y < map.ysize);
    city_ok = (location_ok && (pcity=map_get_city(x,y))
	       && (pcity->owner == game.player_idx));
  }    
  XtSetSensitive(meswin_goto_command, location_ok?True:False);
  XtSetSensitive(meswin_popcity_command, city_ok?True:False);
}


/**************************************************************************
...
**************************************************************************/
void meswin_close_callback(Widget w, XtPointer client_data, 
			      XtPointer call_data)
{
  XtDestroyWidget(meswin_dialog_shell);
  meswin_dialog_shell=0;
}

/****************************************************************
...
*****************************************************************/
void meswin_msg_close(Widget w)
{
  meswin_close_callback(w, NULL, NULL);
}

/**************************************************************************
...
**************************************************************************/
void meswin_goto_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data)
{
  XawListReturnStruct *ret;

  ret=XawListShowCurrent(meswin_list);

  if(ret->list_index!=XAW_LIST_NONE && (xpos[ret->list_index] != 0 || ypos[ret->list_index]!=0))
    center_tile_mapcanvas(xpos[ret->list_index], ypos[ret->list_index]);
}

/**************************************************************************
...
**************************************************************************/
void meswin_popcity_callback(Widget w, XtPointer client_data, 
			     XtPointer call_data)
{
  XawListReturnStruct *ret;
  struct city *pcity;
  int x, y;
  
  ret=XawListShowCurrent(meswin_list);
  if(ret->list_index!=XAW_LIST_NONE) {
    x = xpos[ret->list_index];
    y = ypos[ret->list_index];
    if((x || y) && (pcity=map_get_city(x,y))
       && (pcity->owner == game.player_idx)) {
      if (center_when_popup_city) {
	center_tile_mapcanvas(x,y);
      }
      popup_city_dialog(pcity, 0);
    }
  }
}


