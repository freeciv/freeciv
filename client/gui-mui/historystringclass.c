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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <support.h>

#include <intuition/sghooks.h>
#include <libraries/mui.h>

#include <clib/alib_protos.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include "muistuff.h"

#define INPUTLINE_MAXLINES 200

struct StringNode
{
  struct MinNode  minnode;
  UWORD strlen;
  UBYTE str[1];
};

struct HistoryString_Data
{
  struct Hook edit_hook;

  /* History stuff */
  APTR mempool;

  int lines;

  struct StringNode *current;
  struct MinList    stringlist;
};

HOOKPROTO(string_edit, int, struct SGWork *sgw, ULONG *msg)
{
  DECLARG_3(a0, struct Hook *, hook, a2, struct SGWork *, sgw, a1, ULONG *, msg)

  struct HistoryString_Data *data = hook->h_Data;
  APTR MemPool = data->mempool;

  if (*msg == SGH_KEY)
  {
    struct StringNode *entry;
    ULONG len;

    if (sgw->EditOp == EO_ENTER)
    {
      data->current = NULL;   /* reset pointer */
      len = sgw->NumChars;

      if (len != 0)
      {
        if (data->lines == INPUTLINE_MAXLINES)
        {
          data->lines--;
          entry = (struct StringNode *)RemTail((struct List *)&data->stringlist);
          FreePooled(MemPool, entry, sizeof(struct StringNode) + entry->strlen);
        }

        entry = AllocPooled(MemPool, sizeof(struct StringNode) + len);

        if (entry)
        {
          data->lines++;
          entry->strlen = len;
          strcpy(entry->str, sgw->WorkBuffer);
          AddHead((struct List *)&data->stringlist, (struct Node *)&entry->minnode);
        }
      }
    }
    else
    {
      if (sgw->IEvent->ie_Class == IECLASS_RAWKEY)
      {
        entry = data->current;

        switch (sgw->IEvent->ie_Code)
        {
          case  CURSORUP:
          {
            if ( entry == NULL )
            {
              entry = (struct StringNode *)data->stringlist.mlh_Head;
            }
            else
            {
              entry = (struct StringNode *)entry->minnode.mln_Succ;
            }

            if ( entry->minnode.mln_Succ == NULL )
              return 0;

            break;
          }

          case  CURSORDOWN:
          {
            if ( entry == NULL )
              return 0;

            entry = (struct StringNode *)entry->minnode.mln_Pred;

            if ( entry->minnode.mln_Pred == NULL )
            {
              data->current       = NULL;
              sgw->WorkBuffer[0]  = '\0';
              sgw->BufferPos      = sgw->NumChars = 0;
              sgw->Actions       |= SGA_USE;
              return 0;
            }

            break;
          }

          default: return 0;
        }

        data->current    = entry;
        sgw->BufferPos   = sgw->NumChars = entry->strlen;
        sgw->Actions    |= SGA_USE;

        strcpy(sgw->WorkBuffer, entry->str);
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

    data->stringlist.mlh_Head     = (struct MinNode *) &data->stringlist.mlh_Tail;
    data->stringlist.mlh_TailPred = (struct MinNode *) &data->stringlist.mlh_Head;

		if (!(data->mempool = CreatePool(MEMF_PUBLIC,1024,1024))) /* pool is accessed by another task only */
		{
			CoerceMethod(cl,o,OM_DISPOSE);
			return NULL;
		}

    set(o,MUIA_String_EditHook, &data->edit_hook);
  }
  return (ULONG) o;
}

static ULONG HistoryString_Dispose(struct IClass *cl, Object *o, Msg msg)
{
  struct HistoryString_Data *data = (struct HistoryString_Data *) INST_DATA(cl, o);
  if (data->mempool) DeletePool(data->mempool);
	return NULL;
}

DISPATCHERPROTO(HistoryString_Dispatcher)
{
  switch (msg->MethodID)
  {
	  case OM_NEW: return HistoryString_New(cl, obj, (struct opSet *) msg);
	  case OM_DISPOSE: return HistoryString_Dispose(cl, obj, msg);
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

