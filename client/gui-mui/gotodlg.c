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
#include "map.h"
#include "packets.h"
#include "player.h"
#include "support.h"
#include "unit.h"

#include "clinet.h"
#include "civclient.h"
#include "control.h"
#include "dialogs.h"
#include "mapctrl.h"
#include "mapview.h"
#include "gui_main.h"
#include "gotodlg.h"
#include "muistuff.h"

static Object *goto_wnd;
static Object *goto_cities_listview;
static struct Hook goto_cities_disphook;
static Object *goto_airlift_button;

/****************************************************************
 Display function for the cities listview
*****************************************************************/
HOOKPROTONH(goto_cities_display, void, char **array, struct city *pcity)
{
  static char name[80];
  sz_strlcpy(name, pcity->name);
  /* FIXME: should use unit_can_airlift_to(). */
  if (pcity->airlift) {
    sz_strlcat(name, "(A)");
  }
  *array = name;
}

/****************************************************************
 Callback for a new cities activation in the listview
*****************************************************************/
void goto_cities_active(void)
{
  struct city *pcity;
  DoMethod(goto_cities_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &pcity);
  if (pcity)
  {
    struct unit *punit = get_unit_in_focus();
// remove me
#if DISABLED
    center_tile_mapcanvas(pdestcity->x, pdestcity->y);
#endif
    if (punit && unit_can_airlift_to(punit, pcity))
    {
      set(goto_airlift_button, MUIA_Disabled, FALSE);
      return;
    }
  }
  set(goto_airlift_button, MUIA_Disabled, TRUE);
}

/****************************************************************
 Callback for the Ok button
*****************************************************************/
static void goto_ok(void)
{
  struct city *pcity;
  DoMethod(goto_cities_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &pcity);
  if (pcity)
  {
    struct unit *punit = get_unit_in_focus();
    if (punit)
    {
      send_goto_unit(punit, pcity->tile);
    }
  }
  set(goto_wnd, MUIA_Window_Open, FALSE);
}

/****************************************************************
 Callback for the Airlist button
*****************************************************************/
VOID goto_airlift(void)
{
  struct city *pcity;
  DoMethod(goto_cities_listview, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &pcity);
  if (pcity)
  {
    struct unit *punit = get_unit_in_focus();
    if (punit)
    {
      request_unit_airlift(punit, pcity);
    }
  }
  set(goto_wnd, MUIA_Window_Open, FALSE);
}

/**************************************************************************
...
**************************************************************************/
void update_goto_dialog(void)
{
  int i;

  set(goto_cities_listview, MUIA_NList_Quiet, TRUE);
  DoMethod(goto_cities_listview, MUIM_NList_Clear);

  for (i = 0; i < game.nplayers; i++)
  {
    city_list_iterate(game.players[i].cities, pcity)
      DoMethod(goto_cities_listview, MUIM_NList_InsertSingle, pcity, MUIV_NList_Insert_Bottom);
    city_list_iterate_end;
  }
  set(goto_cities_listview, MUIA_NList_Quiet, FALSE);
  set(goto_airlift_button, MUIA_Disabled, TRUE);
}

/****************************************************************
 popup the goto window
*****************************************************************/
void popup_goto_dialog(void)
{
  Object *goto_ok_button;
  Object *goto_cancel_button;
  if (!goto_wnd)
  {
    goto_cities_disphook.h_Entry = (HOOKFUNC) goto_cities_display;

    goto_wnd = WindowObject,
        MUIA_Window_Title, _("Goto/Airlift Unit"),
        MUIA_Window_ID, MAKE_ID('G','O','T','O'),
        WindowContents, VGroup,
            Child, goto_cities_listview = NListviewObject,
                MUIA_CycleChain, 1,
                MUIA_NListview_NList, NListObject,
                    MUIA_NList_DisplayHook, &goto_cities_disphook,
                    End,
                End,
            Child, HGroup,
                Child, goto_ok_button = MakeButton(_("_Ok")),
                Child, goto_airlift_button = MakeButton(_("_Airlift")),
                Child, goto_cancel_button = MakeButton(_("_Cancel")),
                End,
            End,
        End;
    if (goto_wnd)
    {
      DoMethod(goto_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, goto_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
      DoMethod(goto_cancel_button, MUIM_Notify, MUIA_Pressed, FALSE, goto_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
      DoMethod(goto_ok_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &civstandard_hook, goto_ok);
      DoMethod(goto_airlift_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 3, MUIM_CallHook, &civstandard_hook, goto_airlift);
      DoMethod(goto_cities_listview, MUIM_Notify, MUIA_NList_Active, MUIV_EveryTime, app, 3, MUIM_CallHook, &civstandard_hook, goto_cities_active);
      DoMethod(app, OM_ADDMEMBER, goto_wnd);
    }
  }

  if (goto_wnd)
  {
    update_goto_dialog();
    set(goto_wnd, MUIA_Window_DefaultObject, goto_cities_listview);
    set(goto_wnd, MUIA_Window_Open, TRUE);
  }
}
