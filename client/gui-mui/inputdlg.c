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

#include "fcintl.h"
#include "shared.h"
#include "inputdlg.h"
#include "gui_main.h"
#include "muistuff.h"

/**************************************************************************
 Supply the input information
**************************************************************************/
char *input_dialog_get_input(Object *name_string)
{
  return getstring(name_string);
}

/****************************************************************
 Must be called from the Application object so it is safe to
 dispose the window
*****************************************************************/
static void input_dialog_close_real( Object **ppwnd)
{
  set(*ppwnd,MUIA_Window_Open,FALSE);
  DoMethod(app, OM_REMMEMBER, *ppwnd);
  MUI_DisposeObject(*ppwnd);
}

/****************************************************************
 input_dialog_destroy destroy the object after use
*****************************************************************/
void input_dialog_destroy( Object * wnd)
{
  if(wnd)
  {
    set(wnd, MUIA_Window_Open, FALSE);
    /* Close the window better in the Application Object */
    DoMethod(app, MUIM_Application_PushMethod, app, 4, MUIM_CallHook, &civstandard_hook, input_dialog_close_real, wnd);
  }
}

/****************************************************************
 Popups a window which contains a (window) title, a text input field
 and 2 buttons.
*****************************************************************/
Object *input_dialog_create( Object *parent, char *title, char *text, char *postinputtext,
void *ok_callback, APTR ok_data, void *cancel_callback, APTR cancel_data)
{
  Object *name_string;
  Object *ok_button;
  Object *cancel_button;
  Object *wnd;

  wnd = WindowObject,
    MUIA_Window_Title,title,
    MUIA_Window_ID, MAKE_ID('I','N','P','D'),
    WindowContents, VGroup,
      Child, HGroup,
	Child, MakeLabel(text),
	      Child, name_string = MakeString(text, MAX_LEN_NAME - 1),
	      End,
	  Child, HGroup,
	      Child, ok_button = MakeButton(_("_Ok")),
	      Child, cancel_button = MakeButton(_("_Cancel")),
	      End,
	  End,
      End;

  if(wnd)
  {
    DoMethod(wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, app, 6, MUIM_CallHook, &civstandard_hook,
    cancel_callback, wnd, name_string, cancel_data);
    DoMethod(cancel_button, MUIM_Notify, MUIA_Pressed, FALSE, cancel_button, 6, MUIM_CallHook, &civstandard_hook,
    cancel_callback, wnd, name_string, cancel_data);
    DoMethod(ok_button, MUIM_Notify, MUIA_Pressed, FALSE, cancel_button, 6, MUIM_CallHook, &civstandard_hook,
    ok_callback, wnd, name_string, ok_data);
    DoMethod(name_string, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, cancel_button, 6,
    MUIM_CallHook, &civstandard_hook, ok_callback, wnd, name_string, ok_data);
    setstring(name_string, postinputtext);

    DoMethod(app,OM_ADDMEMBER,wnd);
    SetAttrs(wnd, MUIA_Window_Open, TRUE, MUIA_Window_ActiveObject, name_string, TAG_DONE);
  }
  return wnd;
}
