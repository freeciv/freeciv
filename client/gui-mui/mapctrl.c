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

#include <libraries/mui.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>

#include "capability.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "player.h"
#include "unit.h"

#include "civclient.h"
#include "clinet.h"
#include "colors.h"
#include "control.h"
#include "dialogs.h"
#include "mapview.h"

#include "mapctrl.h"

/* Amiga client stuff */

#include "muistuff.h"
#include "mapclass.h"

IMPORT Object *app;
IMPORT Object *main_turndone_button;

STATIC Object *newcity_wnd;
STATIC Object *newcity_name_string;
STATIC Object *newcity_ok_button;
STATIC Object *newcity_cancel_button;
STATIC LONG newcity_open;

/* Update the workers for a city on the map, when the update is received */
struct city *city_workers_display = NULL;
/* Color to use to display the workers */
int city_workers_color=COLOR_STD_WHITE;

/**************************************************************************
 GUI independend (control.c)
**************************************************************************/
static void build_city(size_t unit_id, char *cityname)
{
  if (unit_id)
  {
    struct packet_unit_request req;
    req.unit_id = unit_id;
    strncpy(req.name, cityname, MAX_LEN_NAME);
    req.name[MAX_LEN_NAME - 1] = '\0';
    send_packet_unit_request(&aconnection, &req, PACKET_UNIT_BUILD_CITY);
  }
}

/* TODO: Better put this in dialogs.c and create a subclass
  of WindowC */

/**************************************************************************
 Callback for the Cancel button in the new city dialog
**************************************************************************/
static void newcity_cancel(void)
{
  set(newcity_wnd, MUIA_Window_Open, FALSE);
  DoMethod(newcity_ok_button, MUIM_KillNotify, MUIA_Pressed);
  DoMethod(newcity_name_string, MUIM_KillNotify, MUIA_String_Acknowledge);
  newcity_open = FALSE;
}

/**************************************************************************
 Callback for the Ok button in the new city dialog
**************************************************************************/
static void newcity_ok(int *uid)
{
  newcity_cancel();
  build_city(*uid, getstring(newcity_name_string));
}

/**************************************************************************
 Popup dialog where the user choose the name of the new city
 punit = (settler) unit which builds the city
 suggestname = suggetion of the new city's name
**************************************************************************/
void popup_newcity_dialog(struct unit *punit, char *suggestname)
{
  if (!newcity_wnd)
  {
    newcity_wnd = WindowObject,
      MUIA_Window_Title, "What should we call our new city?",
      MUIA_Window_ID, 'NEWC',

      WindowContents, VGroup,
	  Child, HGroup,
	      Child, MakeLabel("City_name"),
	      Child, newcity_name_string = MakeString("City_name", MAX_LEN_NAME - 1),
	      End,
	  Child, HGroup,
	      Child, newcity_ok_button = MakeButton("_Ok"),
	      Child, newcity_cancel_button = MakeButton("_Cancel"),
	      End,
	  End,
      End;

    if (newcity_wnd)
    {
      DoMethod(newcity_cancel_button, MUIM_Notify, MUIA_Pressed, FALSE, newcity_cancel_button, 3, MUIM_CallHook, &standart_hook, newcity_cancel);
      DoMethod(app, OM_ADDMEMBER, newcity_wnd);
    }
  }

  if (newcity_wnd && !newcity_open)
  {
    DoMethod(newcity_ok_button, MUIM_Notify, MUIA_Pressed, FALSE, newcity_cancel_button, 4, MUIM_CallHook, &standart_hook, newcity_ok, punit->id);
    DoMethod(newcity_name_string, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, newcity_cancel_button, 4, MUIM_CallHook, &standart_hook, newcity_ok, punit->id);
    setstring(newcity_name_string, suggestname);
    SetAttrs(newcity_wnd,
	     MUIA_Window_Open, TRUE,
	     MUIA_Window_ActiveObject, newcity_name_string,
	     TAG_DONE);

    newcity_open = TRUE;
  }
}

/**************************************************************************
 Activate or deactivate the turn done button.
 Should probably some where else.
**************************************************************************/
void set_turn_done_button_state(int state)
{
  set(main_turndone_button, MUIA_Disabled, !state);
}

/**************************************************************************
 Callback if the user clicks on the map
**************************************************************************/
int main_map_click(struct Map_Click **click)
{
  int xtile = (*click)->x;
  int ytile = (*click)->y;

  do_map_click(xtile, ytile);
  return 0;
}
