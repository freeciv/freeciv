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

#include <libraries/mui.h>
#include <mui/NListview_MCC.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/muimaster.h>

#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "packets.h"
#include "player.h"
#include "shared.h"

#include "gui_main.h"
#include "helpdlg.h"
#include "inteldlg.h"

/* Amiga Client stuff */

#include "autogroupclass.h"
#include "colortextclass.h"
#include "muistuff.h"

static Object * intel_wnd;
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
  DoMethod(app, MUIM_Application_PushMethod, app, 3, MUIM_CallHook, &civstandard_hook, intel_close_real);
}

/****************************************************************
 Callback for the technologies
*****************************************************************/
static void intel_tech( ULONG *tech)
{
  popup_help_dialog_typed( advances[*tech].name, HELP_TECH);
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
  Object *tech_group;

  if (intel_wnd)
    return;

  intel_wnd = WindowObject,
    MUIA_Window_Title, _("Foreign Intelligence Report"),
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
	Child, ScrollgroupObject,
            MUIA_Scrollgroup_FreeVert, FALSE,
	    MUIA_Scrollgroup_Contents, tech_group = AutoGroup, VirtualFrame, End,
	    End,
	End,
    End;

  if (intel_wnd)
  {
    struct city *pcity = find_palace(p);
    int i;

    settextf(title_text, _("Intelligence Information for the %s Empire"), get_nation_name(p->nation));
    settextf(ruler_text, _("Ruler: %s %s"), get_ruler_title(p->government, p->is_male, p->nation), p->name);
    settextf(government_text, _("Government: %s"), get_government_name(p->government));
    settextf(gold_text, _("Gold: %d"), p->economic.gold);
    settextf(tax_text, _("Tax: %d%%"), p->economic.tax);
    settextf(science_text, _("Science: %d%%"), p->economic.science);
    settextf(luxury_text, _("Luxury: %d%%"), p->economic.luxury);
    settextf(researching_text, _("Researching: %s(%d/%d)"),
	     get_tech_name(p, p->research.researching),
	     p->research.bulbs_researched, total_bulbs_required(p));

    settextf(capital_text, _("Capital: %s"), (pcity == NULL) ? _("(Unknown)") : pcity->name);

    for (i = 1; i < game.num_tech_types; i++)
    {
      if (get_invention(p, i) == TECH_KNOWN)
      {
	Object *tech = ColorTextObject,
	    MUIA_ColorText_Contents, advances[i].name,
	    MUIA_ColorText_Background, GetTechBG(i),
	    MUIA_InputMode, MUIV_InputMode_RelVerify,
	    End;
        if (tech)
        {
          DoMethod(tech_group, OM_ADDMEMBER, tech);
          DoMethod(tech, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, intel_tech, i);
        }
      }
    }

    DoMethod(intel_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, app, 3, MUIM_CallHook, &civstandard_hook, intel_close);
    DoMethod(app, OM_ADDMEMBER, intel_wnd);
  }
}

/****************************************************************************
  Update the intelligence dialog for the given player.  This is called by
  the core client code when that player's information changes.
****************************************************************************/
void update_intel_dialog(struct player *p)
{
  /* PORTME */
}
