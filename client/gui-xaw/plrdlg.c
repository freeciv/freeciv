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

#include "game.h"
#include "packets.h"
#include "player.h"

#include "chatline.h"
#include "clinet.h"
#include "diplodlg.h"
#include "gui_stuff.h"
#include "inteldlg.h"
#include "spaceshipdlg.h"

#include "plrdlg.h"

extern Widget toplevel, main_form;
extern Display *display;
extern Atom wm_delete_window;

Widget players_dialog_shell;
Widget players_form;
Widget players_label;
Widget players_list;
Widget players_close_command;
Widget players_int_command;
Widget players_meet_command;
Widget players_sship_command;

void create_players_dialog(void);
void players_close_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data);
void players_meet_callback(Widget w, XtPointer client_data, 
			   XtPointer call_data);
void players_intel_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data);
void players_list_callback(Widget w, XtPointer client_data, 
			   XtPointer call_data);
void players_sship_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data);


/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_players_dialog(void)
{
  if(!players_dialog_shell)
    create_players_dialog();

  xaw_set_relative_position(toplevel, players_dialog_shell, 25, 25);
  XtPopup(players_dialog_shell, XtGrabNone);
}


/****************************************************************
...
*****************************************************************/
void create_players_dialog(void)
{
  players_dialog_shell = XtCreatePopupShell("playerspopup", 
					  topLevelShellWidgetClass,
					  toplevel, NULL, 0);

  players_form = XtVaCreateManagedWidget("playersform", 
				       formWidgetClass, 
				       players_dialog_shell, NULL);

  players_label=XtVaCreateManagedWidget("playerslabel", 
					labelWidgetClass, 
					players_form, NULL);   

   
  players_list = XtVaCreateManagedWidget("playerslist", 
					 listWidgetClass, 
					 players_form, 
					 NULL);

  players_close_command = XtVaCreateManagedWidget("playersclosecommand", 
						  commandWidgetClass,
						  players_form,
						  NULL);

  players_int_command = XtVaCreateManagedWidget("playersintcommand", 
						commandWidgetClass,
						players_form,
                                                XtNsensitive, False,
						NULL);

  players_meet_command = XtVaCreateManagedWidget("playersmeetcommand", 
						 commandWidgetClass,
						 players_form,
						 XtNsensitive, False,
						 NULL);

  players_sship_command = XtVaCreateManagedWidget("playerssshipcommand",
						  commandWidgetClass,
						  players_form,
						  XtNsensitive, False,
						  NULL);

  XtAddCallback(players_list, XtNcallback, players_list_callback, 
		NULL);
  
  XtAddCallback(players_close_command, XtNcallback, players_close_callback, 
		NULL);

  XtAddCallback(players_meet_command, XtNcallback, players_meet_callback, 
		NULL);
  
  XtAddCallback(players_int_command, XtNcallback, players_intel_callback, 
		NULL);

  XtAddCallback(players_sship_command, XtNcallback, players_sship_callback, 
		NULL);
  
  update_players_dialog();

  XtRealizeWidget(players_dialog_shell);
  
  XSetWMProtocols(display, XtWindow(players_dialog_shell), 
		  &wm_delete_window, 1);
  XtOverrideTranslations(players_dialog_shell,
    XtParseTranslationTable("<Message>WM_PROTOCOLS: close-playersdialog()"));
}


/**************************************************************************
...
**************************************************************************/
void update_players_dialog(void)
{
   if(players_dialog_shell) {
    int i;
    Dimension width;
    static char *namelist_ptrs[MAX_NUM_PLAYERS];
    static char namelist_text[MAX_NUM_PLAYERS][256];
    
    for(i=0; i<game.nplayers; i++) {
      char idlebuf[32], statebuf[32], namebuf[32];
      
      if(game.players[i].nturns_idle>3)
	sprintf(idlebuf, "(idle %d turns)", game.players[i].nturns_idle-1);
      else
	idlebuf[0]='\0';
      
      if(game.players[i].is_alive) {
	if(game.players[i].is_connected) {
	  if(game.players[i].turn_done)
	    strcpy(statebuf, "done");
	  else
	    strcpy(statebuf, "moving");
	}
	else
	  statebuf[0]='\0';
      }
      else
	strcpy(statebuf, "R.I.P");
       
      if(game.players[i].ai.control)
	sprintf(namebuf,"*%-15s",game.players[i].name);
      else
        sprintf(namebuf,"%-16s",game.players[i].name);
      sprintf(namelist_text[i], "%-16s %-12s    %c     %-6s   %-15s%s", 
	      namebuf,
	      get_nation_name(game.players[i].nation), 
	      player_has_embassy(game.player_ptr, &game.players[i]) ? 'X':' ',
	      statebuf,
	      game.players[i].addr, 
	      idlebuf);
	 
      namelist_ptrs[i]=namelist_text[i];
    }
    
    XawListChange(players_list, namelist_ptrs, game.nplayers, 0, True);

    XtVaGetValues(players_list, XtNwidth, &width, NULL);
    XtVaSetValues(players_label, XtNwidth, width, NULL); 
  }
}

/**************************************************************************
...
**************************************************************************/
void players_list_callback(Widget w, XtPointer client_data, 
			   XtPointer call_data)

{
  XawListReturnStruct *ret;

  ret=XawListShowCurrent(players_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    struct player *pplayer = &game.players[ret->list_index];
    
    if(pplayer->spaceship.state != SSHIP_NONE)
      XtSetSensitive(players_sship_command, TRUE);
    else
      XtSetSensitive(players_sship_command, FALSE);

    if(pplayer->is_alive && player_has_embassy(game.player_ptr, pplayer)) {
      if(pplayer->is_connected)
	XtSetSensitive(players_meet_command, TRUE);
      else
	XtSetSensitive(players_meet_command, FALSE);
      XtSetSensitive(players_int_command, TRUE);
      return;
    }
  }
  XtSetSensitive(players_meet_command, FALSE);
  XtSetSensitive(players_int_command, FALSE);
}


/**************************************************************************
...
**************************************************************************/
void players_close_callback(Widget w, XtPointer client_data, 
			      XtPointer call_data)
{
  XtDestroyWidget(players_dialog_shell);
  players_dialog_shell=0;
}

/****************************************************************
...
*****************************************************************/
void close_players_dialog_action(Widget w, XEvent *event, 
				 String *argv, Cardinal *argc)
{
  players_close_callback(w, NULL, NULL);
}

/**************************************************************************
...
**************************************************************************/
void players_meet_callback(Widget w, XtPointer client_data, 
			      XtPointer call_data)
{
  XawListReturnStruct *ret;

  ret=XawListShowCurrent(players_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    if(player_has_embassy(game.player_ptr, &game.players[ret->list_index])) {
      struct packet_diplomacy_info pa;
    
      pa.plrno0=game.player_idx;
      pa.plrno1=ret->list_index;
      send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_INIT_MEETING,
				 &pa);
    }
    else {
      append_output_window("Game: You need an embassy to establish a diplomatic meeting.");
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void players_intel_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data)
{
  XawListReturnStruct *ret;

  ret=XawListShowCurrent(players_list);

  if(ret->list_index!=XAW_LIST_NONE)
    if(player_has_embassy(game.player_ptr, &game.players[ret->list_index]))
      popup_intel_dialog(&game.players[ret->list_index]);
}

void players_sship_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data)
{
  XawListReturnStruct *ret;

  ret=XawListShowCurrent(players_list);

  if(ret->list_index!=XAW_LIST_NONE) 
    popup_spaceship_dialog(&game.players[ret->list_index]);
}
