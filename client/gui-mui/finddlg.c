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
#include <string.h>

#include <libraries/mui.h>
#include <mui/NListview_MCC.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>
#include <proto/graphics.h>
#include <proto/utility.h>

#include "fcintl.h"
#include "game.h"
#include "player.h"
#include "mapview.h"
#include "gui_main.h"
#include "muistuff.h"

static Object *find_wnd;
static Object *find_cities_listview;

/****************************************************************
 Updates the contents of the find dialog
*****************************************************************/
void update_find_dialog(void)
{
  int i;

  if (!find_wnd)
    return;

  set(find_cities_listview, MUIA_NList_Quiet, TRUE);
  DoMethod(find_cities_listview, MUIM_NList_Clear);

  for (i = 0; i < game.nplayers; i++)
  {
    city_list_iterate(game.players[i].cities, pcity)
      DoMethod(find_cities_listview, MUIM_NList_InsertSingle, pcity->name, MUIV_NList_Insert_Bottom);
    city_list_iterate_end;
  }

  DoMethod(find_cities_listview, MUIM_NList_Sort);
  set(find_cities_listview, MUIA_NList_Quiet, FALSE);
}

/****************************************************************
 Callback for the Ok Button
*****************************************************************/
static void find_ok(void)
{
  STRPTR string;

  DoMethod(find_cities_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &string);

  if (string)
  {
    struct city *pcity;

    set(find_wnd, MUIA_Window_Open, FALSE);

    if ((pcity = game_find_city_by_name(string)))
      center_tile_mapcanvas(pcity->tile);
  }
}

/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_find_dialog(void)
{
  Object *find_ok_button;
  Object *find_cancel_button;

  if (!find_wnd)
  {
    find_wnd = WindowObject,
        MUIA_Window_Title, _("Find City"),
        WindowContents, VGroup,
            Child, TextObject,
                MUIA_Text_Contents, _("Select a city"),
                MUIA_Text_PreParse, "\33c",
                End,
            Child, find_cities_listview = NListviewObject,
                MUIA_NListview_NList, NListObject,
                    MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
                    MUIA_NList_DestructHook, MUIV_NList_DestructHook_String,
                    End,
                End,
            Child, HGroup,
                Child, find_ok_button = MakeButton(_("_Ok")),
                Child, find_cancel_button = MakeButton(_("_Cancel")),
                End,
            End,
        End;

    if (find_wnd)
    {
      DoMethod(find_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, find_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
      DoMethod(find_cancel_button, MUIM_Notify, MUIA_Pressed, FALSE, find_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
      DoMethod(find_ok_button, MUIM_Notify, MUIA_Pressed, FALSE, find_wnd, 3, MUIM_CallHook, &civstandard_hook, find_ok);
      DoMethod(find_cities_listview, MUIM_Notify, MUIA_NList_DoubleClick, TRUE, find_wnd, 3, MUIM_CallHook, &civstandard_hook, find_ok);
      DoMethod(app, OM_ADDMEMBER, find_wnd);
    }
  }

  if (find_wnd)
  {
    update_find_dialog();
    set(find_wnd, MUIA_Window_Open, TRUE);
  }
}
