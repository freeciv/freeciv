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

#include <mui/NListview_MCC.h>
#include <libraries/mui.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/muimaster.h>

#include "events.h"
#include "game.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"

#include "chatline.h"
#include "citydlg.h"
#include "clinet.h"
#include "colors.h"
#include "mapview.h"
#include "options.h"

#include "messagewin.h"

/* Amiga client stuff */
#include "muistuff.h"

IMPORT Object *app;
IMPORT Object *main_wnd;

STATIC Object *mes_wnd;
STATIC struct Hook mes_consthook;
STATIC struct Hook mes_desthook;
STATIC struct Hook mes_disphook;
STATIC Object *mes_listview;
STATIC Object *mes_close_button;
STATIC Object *mes_goto_button;
STATIC Object *mes_popcity_button;

void create_meswin_dialog(void);

static int delay_meswin_update = 0;

/******************************************************************
 Turn off updating of message window
*******************************************************************/
void meswin_update_delay_on(void)
{
  delay_meswin_update = 1;
}

/******************************************************************
 Turn on updating of message window
*******************************************************************/
void meswin_update_delay_off(void)
{
  delay_meswin_update = 0;
}


/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_meswin_dialog(void)
{
  if (!mes_wnd)
    create_meswin_dialog();
  if (mes_wnd)
  {
    update_meswin_dialog();
    set(mes_wnd, MUIA_Window_Open, TRUE);
  }
}

struct message_entry
{
  char *message;
  int x, y;
  int event;
};

/****************************************************************
 Constructor of a new entry in the message listview
*****************************************************************/
__asm __saveds static struct message_entry *mes_construct(register __a2 APTR pool, register __a1 struct message_entry *entry)
{
  struct message_entry *newentry = (struct message_entry *) AllocVec(sizeof(*newentry), 0);
  if (newentry)
  {
    int len = strlen(entry->message);

    *newentry = *entry;

    if ((newentry->message = (char *) AllocVec(len + 1, 0)))
      strcpy(newentry->message, entry->message);
  }
  return newentry;
}

/**************************************************************************
 Destructor of a entry in the message listview
**************************************************************************/
__asm __saveds static void mes_destruct(register __a2 APTR pool, register __a1 struct message_entry *entry)
{
  if (entry->message)
    FreeVec(entry->message);
  FreeVec(entry);
}

/**************************************************************************
 Display function for the message listview
**************************************************************************/
__asm __saveds static void mes_display(register __a2 char **array, register __a1 struct message_entry *entry)
{
  if (entry)
  {
    *array = entry->message;
  }
  else
  {
    *array = "Messages";
  }
}

/**************************************************************************
 Callback if a message is selected inside the listview
**************************************************************************/
static void mes_active(void)
{
  struct message_entry *entry;
  DoMethod(mes_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &entry);
  if (entry)
  {
    struct city *pcity;
    int x = entry->x, y = entry->y;

    int location_ok;
    int city_ok;

    location_ok = (x || y);
    city_ok = (location_ok && (pcity = map_get_city(x, y)) && (pcity->owner == game.player_idx));

    set(mes_goto_button, MUIA_Disabled, !location_ok);
    set(mes_popcity_button, MUIA_Disabled, !city_ok);
  }
  else
  {
    set(mes_goto_button, MUIA_Disabled, TRUE);
    set(mes_popcity_button, MUIA_Disabled, TRUE);
  }
}

/**************************************************************************
 Callback for the goto button
**************************************************************************/
static void mes_goto(void)
{
  struct message_entry *entry;
  DoMethod(mes_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &entry);
  if (entry)
  {
    center_tile_mapcanvas(entry->x, entry->y);
  }
}

/**************************************************************************
 Callback for the Popcity button
**************************************************************************/
static void mes_popcity(void)
{
  struct message_entry *entry;
  DoMethod(mes_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &entry);
  if (entry)
  {
    struct city *pcity;
    int x, y;

    x = entry->x;
    y = entry->y;

    if ((x || y) && (pcity = map_get_city(x, y)) && (pcity->owner == game.player_idx))
    {
      if (center_when_popup_city)
      {
	center_tile_mapcanvas(x, y);
      }
      popup_city_dialog(pcity, 0);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void create_meswin_dialog(void)
{
  if (mes_wnd)
    return;

  mes_consthook.h_Entry = (HOOKFUNC) mes_construct;
  mes_desthook.h_Entry = (HOOKFUNC) mes_destruct;
  mes_disphook.h_Entry = (HOOKFUNC) mes_display;

  mes_wnd = WindowObject,
    MUIA_Window_Title, "Messages",
    MUIA_Window_ID, 'MESS',
    WindowContents, VGroup,
        Child, mes_listview = NListviewObject,
	    MUIA_NListview_NList, NListObject,
		MUIA_NList_ConstructHook, &mes_consthook,
		MUIA_NList_DestructHook, &mes_desthook,
		MUIA_NList_DisplayHook, &mes_disphook,
		End,
	    End,
	Child, HGroup,
	    Child, mes_close_button = MakeButton("_Close"),
	    Child, mes_goto_button = MakeButton("_Goto Location"),
	    Child, mes_popcity_button = MakeButton("_Popup City"),
	    End,
	End,
    End;

  if (mes_wnd)
  {
    set(mes_goto_button, MUIA_Disabled, TRUE);
    set(mes_popcity_button, MUIA_Disabled, TRUE);
    DoMethod(mes_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, mes_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(mes_close_button, MUIM_Notify, MUIA_Pressed, FALSE, mes_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(mes_goto_button, MUIM_Notify, MUIA_Pressed, FALSE, mes_wnd, 3, MUIM_CallHook, &standart_hook, mes_goto);
    DoMethod(mes_popcity_button, MUIM_Notify, MUIA_Pressed, FALSE, mes_wnd, 3, MUIM_CallHook, &standart_hook, mes_popcity);

    DoMethod(mes_listview, MUIM_Notify, MUIA_NList_Active, MUIV_EveryTime, app, 3, MUIM_CallHook, &standart_hook, mes_active);

    DoMethod(app, OM_ADDMEMBER, mes_wnd);
  }
}

/**************************************************************************
...
**************************************************************************/

void clear_notify_window(void)
{
  if (mes_wnd)
  {
    DoMethod(mes_listview, MUIM_NList_Clear);
  }
}

/**************************************************************************
...
**************************************************************************/
void add_notify_window(struct packet_generic_message *packet)
{
  struct message_entry entry;

  if (!mes_wnd)
  {
    create_meswin_dialog();
    if (!mes_wnd)
      return;
  }

  if (!strncmp(packet->message, "Game: ", 6))
    entry.message = packet->message + 6;
  else
    entry.message = packet->message;

  entry.x = packet->x;
  entry.y = packet->y;
  entry.event = packet->event;

  DoMethod(mes_listview, MUIM_NList_InsertSingle, &entry, MUIV_NList_Insert_Bottom);
}

/**************************************************************************
...
**************************************************************************/
void update_meswin_dialog(void)
{
  if (!mes_wnd)
  {
    create_meswin_dialog();
    if (!mes_wnd)
      return;
  }

  if (xget(mes_listview, MUIA_NList_Entries))
  {
    if (!xget(mes_wnd, MUIA_Window_Open))
    {
      if (!game.player_ptr->ai.control || ai_popup_windows)
	set(mes_wnd, MUIA_Window_Open, TRUE);
    }
  }
}
