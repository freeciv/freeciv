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
#include <string.h>
#include <ctype.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/Toggle.h>     

#include "game.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "unit.h"

#include "civclient.h"
#include "clinet.h"
#include "mapctrl.h"
#include "mapview.h"

#include "gotodlg.h"

void send_unit_info(struct unit *punit);

extern Widget toplevel, main_form;
extern struct player_race races[];

Widget goto_dialog_shell;
Widget goto_form;
Widget goto_label;
Widget goto_viewport;
Widget goto_list;
Widget goto_center_command;
Widget goto_airlift_command;
Widget goto_all_toggle;
Widget goto_cancel_command;

void update_goto_dialog(Widget goto_list);

void goto_cancel_command_callback(Widget w, XtPointer client_data, 
				  XtPointer call_data);
void goto_goto_command_callback(Widget w, XtPointer client_data, 
				  XtPointer call_data);
void goto_airlift_command_callback(Widget w, XtPointer client_data, 
				  XtPointer call_data);
void goto_all_toggle_callback(Widget w, XtPointer client_data, 
			      XtPointer call_data);
void goto_list_callback(Widget w, XtPointer client_data, XtPointer call_data);

static char *dummy_city_list[]={ 
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  0
};

static int ncities_total;
static char **city_name_ptrs;
static int original_x, original_y;

void popup_goto_dialog_action(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  popup_goto_dialog();
}

/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_goto_dialog(void)
{
  Position x, y;
  Dimension width, height;

  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    return;
  if(get_unit_in_focus()==0)
    return;

  get_center_tile_mapcanvas(&original_x, &original_y);
  
  XtSetSensitive(main_form, FALSE);
  
  goto_dialog_shell = XtCreatePopupShell("gotodialog", 
					 transientShellWidgetClass,
					 toplevel, NULL, 0);

  goto_form = XtVaCreateManagedWidget("gotoform", 
				      formWidgetClass, 
				      goto_dialog_shell, NULL);

  goto_label = XtVaCreateManagedWidget("gotolabel", 
				       labelWidgetClass, 
				       goto_form,
				       NULL);

  goto_viewport = XtVaCreateManagedWidget("gotoviewport", 
				      viewportWidgetClass, 
				      goto_form, 
				      NULL);

  goto_list = XtVaCreateManagedWidget("gotolist", 
				      listWidgetClass, 
				      goto_viewport, 
				      XtNlist, 
				      (XtArgVal)dummy_city_list,
				      NULL);

  goto_center_command = XtVaCreateManagedWidget("gotocentercommand", 
						commandWidgetClass,
						goto_form,
						NULL);

  goto_airlift_command = XtVaCreateManagedWidget("gotoairliftcommand", 
						 commandWidgetClass,
						 goto_form,
						 NULL);

  goto_all_toggle = XtVaCreateManagedWidget("gotoalltoggle",
  					    toggleWidgetClass,
					    goto_form,
					    NULL);

  goto_cancel_command = XtVaCreateManagedWidget("gotocancelcommand", 
						commandWidgetClass,
						goto_form,
						NULL);

  XtAddCallback(goto_list, XtNcallback, goto_list_callback, NULL);
  XtAddCallback(goto_center_command, XtNcallback, 
		goto_goto_command_callback, NULL);
  XtAddCallback(goto_airlift_command, XtNcallback, 
		goto_airlift_command_callback, NULL);
  XtAddCallback(goto_all_toggle, XtNcallback,
		goto_all_toggle_callback, NULL);
  XtAddCallback(goto_cancel_command, XtNcallback, 
		goto_cancel_command_callback, NULL);

  XtRealizeWidget(goto_dialog_shell);

  update_goto_dialog(goto_list);

  XtVaGetValues(toplevel, XtNwidth, &width, XtNheight, &height, NULL);

  XtTranslateCoords(toplevel, (Position) width/10, (Position) height/10,
		    &x, &y);
  XtVaSetValues(goto_dialog_shell, XtNx, x, XtNy, y, NULL);

  XtPopup(goto_dialog_shell, XtGrabNone);

  /* force refresh of viewport so the scrollbar is added.
   * Buggy sun athena requires this */
  XtVaSetValues(goto_viewport, XtNforceBars, True, NULL);
}

struct city *get_selected_city(void)
{
  XawListReturnStruct *ret;
  ret=XawListShowCurrent(goto_list);
  if(ret->list_index==XAW_LIST_NONE)
    return 0;
  
  if(strlen(ret->string)>3 && strcmp(ret->string+strlen(ret->string)-3, "(A)")==0) {
    char name[MAX_LENGTH_NAME];
    strncpy(name, ret->string, strlen(ret->string)-3);
    name[strlen(ret->string)-3]='\0';
    return game_find_city_by_name(name);
  }
  return game_find_city_by_name(ret->string);
}

/**************************************************************************
...
**************************************************************************/
void update_goto_dialog(Widget goto_list)
{
  int i, j;
  Boolean all_cities;

  XtVaGetValues(goto_all_toggle, XtNstate, &all_cities, NULL);
  
  if(all_cities)
    for(i=0, ncities_total=0; i<game.nplayers; i++)
      ncities_total+=city_list_size(&game.players[i].cities);
  else
      ncities_total=city_list_size(&game.player_ptr->cities);

  city_name_ptrs=fc_malloc(ncities_total*sizeof(char*));
  
  for(i=0, j=0; i<game.nplayers; i++) {
    if(!all_cities && i!=game.player_idx) continue;
    city_list_iterate(game.players[i].cities, pcity) {
      char name[MAX_LENGTH_NAME+3];
      strcpy(name, pcity->name);
      if(pcity->improvements[B_AIRPORT]==1)
	strcat(name, "(A)");
      *(city_name_ptrs+j++)=mystrdup(name);
    }
    city_list_iterate_end;
  }

  if(ncities_total) {
    qsort(city_name_ptrs,ncities_total,sizeof(char *),string_ptr_compare);
    XawListChange(goto_list, city_name_ptrs, ncities_total, 0, True);
  }
}

/**************************************************************************
...
**************************************************************************/
void popdown_goto_dialog(void)
{
  int i;
  
  for(i=0; i<ncities_total; i++)
    free(*(city_name_ptrs+i));
  
  XtDestroyWidget(goto_dialog_shell);
  free(city_name_ptrs);
  XtSetSensitive(main_form, TRUE);
}

/**************************************************************************
...
**************************************************************************/
void goto_list_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  XawListReturnStruct *ret;
  ret=XawListShowCurrent(goto_list);
  
  if(ret->list_index!=XAW_LIST_NONE) {
    struct city *pdestcity;
    if((pdestcity=get_selected_city())) {
      struct unit *punit=get_unit_in_focus();
      center_tile_mapcanvas(pdestcity->x, pdestcity->y);
      if(punit && unit_can_airlift_to(punit, pdestcity)) {
	XtSetSensitive(goto_airlift_command, True);
	return;
      }
    }
  }
  XtSetSensitive(goto_airlift_command, False);
}

/**************************************************************************
...
**************************************************************************/
void goto_airlift_command_callback(Widget w, XtPointer client_data, 
				  XtPointer call_data)
{
  struct city *pdestcity=get_selected_city();
  if(pdestcity) {
    struct unit *punit=get_unit_in_focus();
    if(punit) {
      struct unit req_unit;
      req_unit=*punit;
      req_unit.x=pdestcity->x;
      req_unit.y=pdestcity->y;
      send_unit_info(&req_unit);
    }
  }
  popdown_goto_dialog();
}

/**************************************************************************
...
**************************************************************************/
void goto_all_toggle_callback(Widget w, XtPointer client_data, 
			      XtPointer call_data)
{
  update_goto_dialog(goto_list);
}

/**************************************************************************
...
**************************************************************************/
void goto_goto_command_callback(Widget w, XtPointer client_data, 
				  XtPointer call_data)
{
  struct city *pdestcity=get_selected_city();
  if(pdestcity) {
    struct unit *punit=get_unit_in_focus();
    if(punit) {
      struct packet_unit_request req;
      req.unit_id=punit->id;
      req.name[0]='\0';
      req.x=pdestcity->x;
      req.y=pdestcity->y;
      send_packet_unit_request(&aconnection, &req, PACKET_UNIT_GOTO_TILE);
    }
  }
  popdown_goto_dialog();
}

/**************************************************************************
...
**************************************************************************/
void goto_cancel_command_callback(Widget w, XtPointer client_data, 
				  XtPointer call_data)
{
  center_tile_mapcanvas(original_x, original_y);
  popdown_goto_dialog();
}
