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

#include <libraries/mui.h>
#include <mui/NListview_MCC.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/muimaster.h>

#include "game.h"
#include "government.h"
#include "packets.h"
#include "player.h"
#include "shared.h"

#include "inteldlg.h"

/* Amiga Client stuff */

#include "muistuff.h"

IMPORT Object *app;

STATIC Object * intel_wnd;

static void intel_create_dialog(struct player *p);

/****************************************************************
... 
*****************************************************************/
void popup_intel_dialog(struct player *p)
{
  if (!intel_wnd)
    intel_create_dialog(p);
  if (intel_wnd)
  {
    set(intel_wnd, MUIA_Window_Open, TRUE);
  }
}

/****************************************************************
 Must be called from the Application object so it is safe to
 dispose the window
*****************************************************************/
static void intel_close_real(void)
{
  set(intel_wnd, MUIA_Window_Open, FALSE);
  DoMethod(app, OM_REMMEMBER, intel_wnd);
  MUI_DisposeObject(intel_wnd);
  intel_wnd = NULL;
}

/****************************************************************
 Close the Inteligence window 
*****************************************************************/
static void intel_close(void)
{
  set(intel_wnd, MUIA_Window_Open, FALSE);
  DoMethod(app, MUIM_Application_PushMethod, app, 3, MUIM_CallHook, &standart_hook, intel_close_real);
}

/****************************************************************
 Create the Inteligence window for the given player
*****************************************************************/
static void intel_create_dialog(struct player *p)
{
  Object *title_text;
  Object *ruler_text;
  Object *government_text;
  Object *gold_text;
  Object *tax_text;
  Object *science_text;
  Object *luxury_text;
  Object *researching_text;
  Object *capital_text;
  Object *research_listview;

  if (intel_wnd)
    return;

  intel_wnd = WindowObject,
    MUIA_Window_Title, "Foreign Intelligence Report",
    WindowContents, VGroup,
	Child, title_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	Child, ruler_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	Child, government_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	Child, capital_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	Child, HGroup,
	    Child, tax_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	    Child, science_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	    Child, luxury_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	    End,
	Child, HGroup,
	    Child, gold_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	    Child, researching_text = TextObject, MUIA_Text_PreParse, "\33c", End,
	    End,
	Child, research_listview = NListviewObject,
	    MUIA_NListview_NList, NListObject,
		MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
		MUIA_NList_DestructHook, MUIV_NList_DestructHook_String,
		End,
	    End,
	End,
    End;

  if (intel_wnd)
  {
    struct city *pcity = find_palace(p);
    int i;

    settextf(title_text, "Intelligence Information for the %s Empire", get_nation_name(p->nation));
    settextf(ruler_text, "Ruler: %s %s", get_ruler_title(p->government, p->is_male, p->nation), p->name);
    settextf(government_text, "Government: %s", get_government_name(p->government));
    settextf(gold_text, "Gold: %d", p->economic.gold);
    settextf(tax_text, "Tax: %d%%", p->economic.tax);
    settextf(science_text, "Science: %d%%", p->economic.science);
    settextf(luxury_text, "Luxury: %d%%", p->economic.luxury);
    if (p->research.researching != A_NONE)
    {
      settextf(researching_text, "Researching: %s(%d/%d)",
	       advances[p->research.researching].name,
	       p->research.researched,
	       research_time(p));
    }
    else
    {
      settextf(researching_text, "Researching Future Tech. %d: %d/%d",
	  ((p->future_tech) + 1), p->research.researched, research_time(p));
    }

    settextf(capital_text, "Capital: %s", (pcity == NULL) ? "(Unknown)" : pcity->name);

    for (i = 1; i < game.num_tech_types; i++)
    {
      if (get_invention(p, i) == TECH_KNOWN)
      {
	if (get_invention(game.player_ptr, i) == TECH_KNOWN)
	{
	  DoMethod(research_listview, MUIM_NList_InsertSingle, advances[i].name, MUIV_NList_Insert_Bottom);
	}
	else
	{
	  char buf[128];
	  sprintf(buf, "%s*", advances[i].name);
	  DoMethod(research_listview, MUIM_NList_InsertSingle, buf, MUIV_NList_Insert_Bottom);
	}
      }
    }

    DoMethod(intel_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, app, 3, MUIM_CallHook, &standart_hook, intel_close);
    DoMethod(app, OM_ADDMEMBER, intel_wnd);
  }
}

