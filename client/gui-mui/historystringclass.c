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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <support.h>

#include <intuition/sghooks.h>
#include <libraries/mui.h>

#include <clib/alib_protos.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include "muistuff.h"

#define INPUTLINE_MAXLINES 20
#define INPUTLINE_MAXCHARS 256

struct HistoryString_Data
{
  struct Hook edit_hook;

  /* History stuff */
  char lines[INPUTLINE_MAXLINES][INPUTLINE_MAXCHARS];
  int line;
  int maxline;
};


HOOKPROTO(string_edit, int, struct SGWork *sgw, ULONG *msg)
{
  struct HistoryString_Data *data = hook->h_Data;
  if (*msg != SGH_KEY) return 0;

  if (sgw->EditOp == EO_ENTER)
  {
    if (data->line >= INPUTLINE_MAXLINES - 1 && data->line)
    {
      /* The end of the histrory buffer is reached */
      CopyMem(data->lines[1],data->lines[0],INPUTLINE_MAXCHARS*(INPUTLINE_MAXLINES-1));
      data->maxline = --data->line;
    }

    if (data->line < INPUTLINE_MAXLINES - 1)
    {
      /* Copy the current line to the history buffer */
      mystrlcpy(data->lines[data->line],sgw->WorkBuffer,INPUTLINE_MAXCHARS);
      data->maxline = ++data->line;
      data->lines[data->line][0] = 0;
    }
  } else
  {
    if (sgw->IEvent->ie_Class == IECLASS_RAWKEY)
    {
      switch (sgw->IEvent->ie_Code)
      {
        case  CURSORUP:
	      if(data->line)
	      {
                data->line--;
	        mystrlcpy(sgw->WorkBuffer,data->lines[data->line],INPUTLINE_MAXCHARS);
                sgw->Actions |= SGA_USE;
                sgw->BufferPos = sgw->NumChars = strlen(sgw->WorkBuffer);
	      }
	      break;

        case  CURSORDOWN:
	      if (data->line < data->maxline)
	      {
                data->line++;
	        mystrlcpy(sgw->WorkBuffer,data->lines[data->line],INPUTLINE_MAXCHARS);
                sgw->Actions |= SGA_USE;
                sgw->BufferPos = sgw->NumChars = strlen(sgw->WorkBuffer);
	      }
	      break;
      }	     
    }
  }
  return 0;
}

static ULONG HistoryString_New(struct IClass *cl, Object * o, struct opSet *msg)
{
  if ((o = (Object *) DoSuperMethodA(cl, o, (Msg) msg)))
  {
    struct HistoryString_Data *data = (struct HistoryString_Data *) INST_DATA(cl, o);
    data->edit_hook.h_Entry = (HOOKFUNC)string_edit;
    data->edit_hook.h_Data = data;
    set(o,MUIA_String_EditHook, &data->edit_hook);
  }
  return (ULONG) o;
}

DISPATCHERPROTO(HistoryString_Dispatcher)
{
  switch (msg->MethodID)
  {
  case OM_NEW:
    return HistoryString_New(cl, obj, (struct opSet *) msg);
  }
  return (DoSuperMethodA(cl, obj, msg));
}

struct MUI_CustomClass *CL_HistoryString;

BOOL create_historystring_class(void)
{
  if ((CL_HistoryString = MUI_CreateCustomClass(NULL, MUIC_String, NULL, sizeof(struct HistoryString_Data), (APTR) HistoryString_Dispatcher)))
      return TRUE;
  return FALSE;
}

VOID delete_historystring_class(void)
{
  if (CL_HistoryString)
    MUI_DeleteCustomClass(CL_HistoryString);
}
