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

#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "clinet.h"
#include "colors.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_main.h"
#include "helpdlg.h"
#include "mapctrl.h"
#include "mapview.h"
#include "repodlgs.h"
#include "spaceship.h"
#include "tilespec.h"

#include "spaceshipdlg.h"

/* Amiga Client Stuff */
#include "muistuff.h"
#include "mapclass.h"

struct spaceship_dialog
{
  struct player *pplayer;

  Object *wnd;
  Object *info_text;
  Object *spaceship_area;
  Object *launch_button;
};

#define SPECLIST_TAG dialog
#define SPECLIST_TYPE struct spaceship_dialog
#include "speclist.h"

#define dialog_list_iterate(dialoglist, pdialog) \
    TYPED_LIST_ITERATE(struct spaceship_dialog, dialoglist, pdialog)
#define dialog_list_iterate_end  LIST_ITERATE_END

static struct dialog_list dialog_list;
static bool dialog_list_has_been_initialised = FALSE;

struct spaceship_dialog *get_spaceship_dialog(struct player *pplayer);
struct spaceship_dialog *create_spaceship_dialog(struct player *pplayer);
void close_spaceship_dialog(struct spaceship_dialog *pdialog);

void spaceship_dialog_update_image(struct spaceship_dialog *pdialog);
void spaceship_dialog_update_info(struct spaceship_dialog *pdialog);

/****************************************************************
...
*****************************************************************/
struct spaceship_dialog *get_spaceship_dialog(struct player *pplayer)
{
  if (!dialog_list_has_been_initialised) {
    dialog_list_init(&dialog_list);
    dialog_list_has_been_initialised = TRUE;
  }

  dialog_list_iterate(dialog_list, pdialog) {
    if (pdialog->pplayer == pplayer) {
      return pdialog;
    }
  } dialog_list_iterate_end;

  return NULL;
}

/****************************************************************
...
*****************************************************************/
void refresh_spaceship_dialog(struct player *pplayer)
{
  struct spaceship_dialog *pdialog;
  struct player_spaceship *pship;

  if (!(pdialog = get_spaceship_dialog(pplayer)))
    return;

  pship = &(pdialog->pplayer->spaceship);

  if (game.spacerace
      && pplayer->player_no == game.player_idx
      && pship->state == SSHIP_STARTED
      && pship->success_rate > 0)
  {
    set(pdialog->launch_button, MUIA_Disabled, FALSE);
  }
  else
  {
    set(pdialog->launch_button, MUIA_Disabled, TRUE);
  }

  spaceship_dialog_update_info(pdialog);
  spaceship_dialog_update_image(pdialog);
}

/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_spaceship_dialog(struct player *pplayer)
{
  struct spaceship_dialog *pdialog;

  if (!(pdialog = get_spaceship_dialog(pplayer)))
    pdialog = create_spaceship_dialog(pplayer);

  if (pdialog)
    set(pdialog->wnd, MUIA_Window_Open, TRUE);
}

/****************************************************************
popdown the dialog 
*****************************************************************/
void popdown_spaceship_dialog(struct player *pplayer)
{
  struct spaceship_dialog *pdialog;

  if ((pdialog = get_spaceship_dialog(pplayer)))
    close_spaceship_dialog(pdialog);
}

/****************************************************************
 Must be called from the Application object so it is safe to
 dispose the window
*****************************************************************/
static void space_close_real(struct spaceship_dialog **ppdialog)
{
  close_spaceship_dialog(*ppdialog);
}

/****************************************************************
 Close the spaceship window
*****************************************************************/
static void space_close(struct spaceship_dialog ** ppdialog)
{
  set((*ppdialog)->wnd, MUIA_Window_Open, FALSE);
  DoMethod(app, MUIM_Application_PushMethod, app, 4, MUIM_CallHook, &civstandard_hook, space_close_real, *ppdialog);
}

/****************************************************************
 Callback for the launch button
*****************************************************************/
static void space_launch(struct spaceship_dialog ** ppdialog)
{
  struct packet_spaceship_action packet;

  packet.action = SSHIP_ACT_LAUNCH;
  packet.num = 0;
  send_packet_spaceship_action(&aconnection, &packet);
}

/****************************************************************
...
*****************************************************************/
struct spaceship_dialog *create_spaceship_dialog(struct player *pplayer)
{
  Object *close_button;

  struct spaceship_dialog *pdialog;
  pdialog = (struct spaceship_dialog *) AllocVec(sizeof(struct spaceship_dialog), 0x10000);
  if (!pdialog)
    return NULL;

  pdialog->pplayer = pplayer;

  pdialog->wnd = WindowObject,
    MUIA_Window_Title, pplayer->name,
    MUIA_Window_ID, MAKE_ID('S','P','A',(pplayer->player_no & 0xff)),
    WindowContents, VGroup,
	Child, HGroup,
	    Child, ScrollgroupObject,
		MUIA_Scrollgroup_Contents, HGroupV,
		    VirtualFrame,
		    Child, pdialog->spaceship_area = MakeSpaceShip(&pplayer->spaceship),
		    End,
		End,
            Child, BalanceObject, End,
	    Child, VGroup,
	        MUIA_HorizWeight, 0, 
		Child, HVSpace,
		Child, HGroup,
		    TextFrame,
		    Child, MakeLabel(_("Population:\n"
				     "Support:\n"
				     "Energy:\n"
				     "Mass:\n"
				     "Travel time:\n"
				     "Success prob.:\n"
				     "Year of arrival:")),
		    Child, pdialog->info_text = TextObject,
			End,
		    End,
		Child, pdialog->launch_button = MakeButton(_("_Launch")),
		Child, HVSpace,
		End,
	    End,
	Child, HGroup,
	    Child, close_button = MakeButton(_("_Close")),
	    End,
	End,
    End;

  if (pdialog->wnd)
  {
    DoMethod(pdialog->wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, pdialog->wnd, 4, MUIM_CallHook, &civstandard_hook, space_close, pdialog);
    DoMethod(close_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, space_close, pdialog);
    DoMethod(pdialog->launch_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 4, MUIM_CallHook, &civstandard_hook, space_launch, pdialog);

    DoMethod(app, OM_ADDMEMBER, pdialog->wnd);

    dialog_list_insert(&dialog_list, pdialog);
    refresh_spaceship_dialog(pdialog->pplayer);
    return pdialog;
  }
  FreeVec(pdialog);
  return NULL;
}

/****************************************************************
...
*****************************************************************/
void spaceship_dialog_update_info(struct spaceship_dialog *pdialog)
{
  char buf[512], arrival[16] = "-   ";
  struct player_spaceship *pship = &(pdialog->pplayer->spaceship);

  if (pship->state == SSHIP_LAUNCHED)
  {
    sz_strlcpy(arrival, textyear((int) (pship->launch_year
				    + (int) pship->travel_time)));
  }
  my_snprintf(buf, sizeof(buf),
	  _("%5d\n"
	  "%5d %%\n"
	  "%5d %%\n"
	  "%5d tons\n"
	  "%5.1f years\n"
	  "%5d %%\n"
	  "%8s"),
	  pship->population,
	  (int) (pship->support_rate * 100.0),
	  (int) (pship->energy_rate * 100.0),
	  pship->mass,
	  (float) (0.1 * ((int) (pship->travel_time * 10.0))),
	  (int) (pship->success_rate * 100.0),
	  arrival);

  set(pdialog->info_text, MUIA_Text_Contents, buf);
}

/****************************************************************
 Should also check connectedness, and show non-connected
 parts differently.
*****************************************************************/
void spaceship_dialog_update_image(struct spaceship_dialog *pdialog)
{
}

/****************************************************************
...
*****************************************************************/
void close_spaceship_dialog(struct spaceship_dialog *pdialog)
{
  if (pdialog)
  {
    set(pdialog->wnd, MUIA_Window_Open, FALSE);
    DoMethod(app, OM_REMMEMBER, pdialog->wnd);
    MUI_DisposeObject(pdialog->wnd);
    FreeVec(pdialog);
  }
}
