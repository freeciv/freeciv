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

#include <libraries/mui.h>
#include <clib/alib_protos.h>
#include <proto/muimaster.h>

#include "events.h"
#include "fcintl.h"
#include "options.h"
#include "messagedlg.h"
#include "gui_main.h"

/* MUI Stuff */
#include "muistuff.h"

static struct message_option_object {
  Object *Out;
  Object *Mes;
  Object *Pop;
} message_option_objects[E_LAST];

/**************************************************************************
 Callback for the Ok button
************************************************************************* */
static void message_option_ok(void)
{
  LONG i, j;

  for (i = 0; i < E_LAST; i++) {
    j = 0;
    if(xget(message_option_objects[i].Out, MUIA_Selected)) j |= MW_OUTPUT;
    if(xget(message_option_objects[i].Mes, MUIA_Selected)) j |= MW_MESSAGES;
    if(xget(message_option_objects[i].Pop, MUIA_Selected)) j |= MW_POPUP;
    messages_where[sorted_events[i]] = j;
  }
}

/**************************************************************************
... 
**************************************************************************/
void popup_messageopt_dialog(void)
{
  static Object *option_wnd = 0;
  LONG i, err = 0;

  if(!option_wnd)
  {
    Object *group[2], *o;
    Object *ok_button, *cancel_button;

    option_wnd = WindowObject,
      MUIA_Window_ID, MAKE_ID('O','P','T','M'),
      MUIA_Window_Title, _("Message Options"),
      WindowContents, VGroup,
        Child, TextObject,
          MUIA_Text_Contents,_("Where to Display Messages\n"
          "Out = Output window, Mes = Messages window, Pop = Popup individual window"),
          MUIA_Text_PreParse, "\33c",
          End,
        Child, ScrollgroupObject,
	  MUIA_Scrollgroup_Contents, HGroupV,
          Child, HGroup,
            Child, HSpace(0),
            Child, group[0] = ColGroup(4), End,
            Child, HSpace(0),
            End,
          Child, HGroup,
            Child, HSpace(0),
            Child, group[1] = ColGroup(4), End,
            Child, HSpace(0),
            End,
          End,
          VirtualFrame, End,
        Child, HGroup,
          Child, ok_button = MakeButton(_("_Ok")),
          Child, cancel_button = MakeButton(_("_Cancel")),
          End,
        End,
      End;

    if(option_wnd)
    {
      for (i = 0; i < 2 && !err; i++) {
        if((o = MakeLabel("")))     DoMethod(group[i], OM_ADDMEMBER, o); else err++;
        if((o = MakeLabel(_("Out:")))) DoMethod(group[i], OM_ADDMEMBER, o); else err++;
        if((o = MakeLabel(_("Mes:")))) DoMethod(group[i], OM_ADDMEMBER, o); else err++;
        if((o = MakeLabel(_("Pop:")))) DoMethod(group[i], OM_ADDMEMBER, o); else err++;
      }
      for(i=0; i < E_LAST && !err; i++)
      {
        if((o = MakeLabelLeft(get_message_text(sorted_events[i]))))
          DoMethod(group[i < (E_LAST/2) ? 0: 1], OM_ADDMEMBER, o);
        else
          err++;

        if((message_option_objects[i].Out = MakeCheck(get_message_text(sorted_events[i]), FALSE)))
          DoMethod(group[i < (E_LAST/2) ? 0: 1], OM_ADDMEMBER, message_option_objects[i].Out);
        else
          err++;

        if((message_option_objects[i].Mes = MakeCheck(get_message_text(sorted_events[i]), FALSE)))
          DoMethod(group[i < (E_LAST/2) ? 0: 1], OM_ADDMEMBER, message_option_objects[i].Mes);
        else
          err++;

        if((message_option_objects[i].Pop = MakeCheck(get_message_text(sorted_events[i]), FALSE)))
          DoMethod(group[i < (E_LAST/2) ? 0: 1], OM_ADDMEMBER, message_option_objects[i].Pop);
        else
          err++;
      }
      if(E_LAST & 1) /* uneven number of entries */
      {
        if((o = MakeLabel("")))
          DoMethod(group[0], OM_ADDMEMBER, o);
        else
          err++;
      }

      if(!err)
      {
        DoMethod(option_wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, option_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
        DoMethod(ok_button, MUIM_Notify, MUIA_Pressed, FALSE, option_wnd, 3, MUIM_CallHook, &civstandard_hook, message_option_ok);
        DoMethod(ok_button, MUIM_Notify, MUIA_Pressed, FALSE, option_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
        DoMethod(cancel_button, MUIM_Notify, MUIA_Pressed, FALSE, option_wnd, 3, MUIM_Set, MUIA_Window_Open, FALSE);
        DoMethod(app, OM_ADDMEMBER, option_wnd);
      }
      else
      {
        MUI_DisposeObject(option_wnd); option_wnd = 0; /* something went wrong */
      }
    }
  }

  if(option_wnd)
  {
    for (i = 0; i < E_LAST; i++) {
      setcheckmark(message_option_objects[i].Out, messages_where[sorted_events[i]] & MW_OUTPUT);
      setcheckmark(message_option_objects[i].Mes, messages_where[sorted_events[i]] & MW_MESSAGES);
      setcheckmark(message_option_objects[i].Pop, messages_where[sorted_events[i]] & MW_POPUP);
    }
    set(option_wnd, MUIA_Window_Open, TRUE);
  }
}
