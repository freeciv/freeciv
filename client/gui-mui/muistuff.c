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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libraries/mui.h>
#include <libraries/gadtools.h>
#include <mui/NListview_MCC.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>

#include "support.h"

#include "muistuff.h"


/****************************************************************
 Amiga List support functions
*****************************************************************/

struct MinNode *Node_Next(APTR node)
{
  if (node == NULL)
    return NULL;
  if (((struct MinNode *) node)->mln_Succ == NULL)
    return NULL;
  if (((struct MinNode *) node)->mln_Succ->mln_Succ == NULL)
    return NULL;
  return ((struct MinNode *) node)->mln_Succ;
}

struct MinNode *Node_Prev(APTR node)
{
  if (node == NULL)
    return NULL;
  if (((struct MinNode *) node)->mln_Pred == NULL)
    return NULL;
  if (((struct MinNode *) node)->mln_Pred->mln_Pred == NULL)
    return NULL;
  return ((struct MinNode *) node)->mln_Pred;
}

struct MinNode *List_First(APTR list)
{
  if (!((struct MinList *) list)->mlh_Head)
    return NULL;

  if (((struct MinList *) list)->mlh_Head->mln_Succ == NULL)
    return NULL;
  return ((struct MinList *) list)->mlh_Head;
}

struct MinNode *List_Last(APTR list)
{
  if (!((struct MinList *) list)->mlh_TailPred)
    return NULL;

  if (((struct MinList *) list)->mlh_TailPred->mln_Pred == NULL)
    return NULL;
  return ((struct MinList *) list)->mlh_TailPred;
}

ULONG List_Length(APTR list)
{
  struct MinNode *node = List_First(list);
  ULONG len = 0;
  while (node)
  {
    len++;
    node = Node_Next(node);
  }
  return len;
}

struct MinNode *List_Find(APTR list, ULONG num)
{
  struct MinNode *node = List_First(list);
  while (num--)
  {
    if (!(node = Node_Next(node)))
      break;
  }
  return node;
}

/****************************************************************
 Duplicates a string. Must be freed via FreeVec()
*****************************************************************/
STRPTR StrCopy(const STRPTR str)
{
  STRPTR dest = 0;

  if(str)
  {
    LONG i;

    i = strlen(str) + 1;
    if((dest = (STRPTR) AllocVec(i, 0)))
      CopyMem(str, dest, i);
  }

  return dest;
}

/****************************************************************
 Some BOOPSI and MUI support functions
*****************************************************************/

LONG xget(Object * obj, ULONG attribute)
{
  LONG x;
  get(obj, attribute, &x);
  return (x);
}

char *getstring(Object * obj)
{
  return ((char *) xget(obj, MUIA_String_Contents));
}

BOOL getbool(Object * obj)
{
  return ((BOOL) xget(obj, MUIA_Selected));
}

VOID activate(Object * obj)
{
  if (obj)
  {
    Object *wnd = (Object *) xget(obj, MUIA_WindowObject);
    if (wnd)
    {
      set(wnd, MUIA_Window_ActiveObject, obj);
    }
  }
}

VOID settext(Object * obj, STRPTR text)
{
  STRPTR oldtext = (STRPTR) xget(obj, MUIA_Text_Contents);
  if (oldtext && text)
  {
    if (strcmp(oldtext, text) == 0)
      return;
  }

  set(obj, MUIA_Text_Contents, text);
}

VOID vsettextf(Object * obj, STRPTR format, APTR args)
{
  char buf[1024];
  my_vsnprintf(buf, sizeof(buf), format, args);
  settext(obj, buf);
}

VOID settextf(Object * obj, STRPTR format,...)
{
  va_list list;
  va_start(list, format);

  vsettextf(obj, format, list);
}

ULONG DoSuperNew(struct IClass *cl, Object * obj, ULONG tag1,...)
{
  return (DoSuperMethod(cl, obj, OM_NEW, &tag1, NULL));
}

Object *MakeMenu(struct NewMenu * menu)
{
  return MUI_MakeObject(MUIO_MenustripNM, menu, MUIO_MenustripNM_CommandKeyCheck);
}

Object *MakeLabel(STRPTR str)
{
  return (MUI_MakeObject(MUIO_Label, str, 0));
}

Object *MakeLabelLeft(STRPTR str)
{
  return (MUI_MakeObject(MUIO_Label, str, MUIO_Label_LeftAligned));
}

Object *MakeString(STRPTR label, LONG maxlen)
{
  Object *obj = MUI_MakeObject(MUIO_String, label, maxlen);

  if (obj)
  {
    set(obj, MUIA_CycleChain, 1);
  }

  return (obj);
}

Object *MakeButton(STRPTR str)
{
  Object *obj = MUI_MakeObject(MUIO_Button, str);
  if (obj)
  {
    SetAttrs(obj,
	     MUIA_CycleChain, 1,
	     TAG_DONE);
  }
  return obj;
}

Object *MakeInteger(STRPTR label)
{
  Object *obj = MUI_MakeObject(MUIO_String, label, 10);

  if (obj)
  {
    SetAttrs(obj,
	     MUIA_CycleChain, 1,
	     MUIA_String_Accept, "0123456789",
	     MUIA_String_Integer, 0,
	     TAG_DONE);
  }

  return (obj);
}

Object *MakeCycle(STRPTR label, STRPTR * array)
{
  Object *obj = MUI_MakeObject(MUIO_Cycle, label, array);
  if (obj)
    set(obj, MUIA_CycleChain, 1);
  return (obj);
}

Object *MakeRadio(STRPTR label, STRPTR * array)
{
  Object *obj = MUI_MakeObject(MUIO_Radio, label, array);
  if (obj)
    set(obj, MUIA_CycleChain, 1);
  return (obj);
}

Object *MakeCheck(STRPTR label, ULONG check)
{
  Object *obj = MUI_MakeObject(MUIO_Checkmark, label);
  if (obj)
    set(obj, MUIA_CycleChain, 1);
  return (obj);
}

VOID DisposeAllChilds(Object *o)
{
  struct List *child_list = (struct List*)xget(o,MUIA_Group_ChildList);
  Object *cstate = (Object *)child_list->lh_Head;
  Object *child;

  while ((child = (Object*)NextObject(&cstate)))
  {
    DoMethod(o,OM_REMMEMBER,child);
    MUI_DisposeObject(child);
  }
}

struct Hook civstandard_hook;

/****************************************************************
 Standard Hook function, so we can easily call functions via
 MUIM_CallHook
*****************************************************************/
HOOKPROTONHNO(Standard_Hook_Func, void, ULONG * funcptr)
{
  void (*func) (ULONG *) = (void (*)(ULONG *)) (*funcptr);
  if (func)
    func(funcptr + 1);
}

/****************************************************************
 Initialize the standard hook
*****************************************************************/
void init_civstandard_hook(void)
{
  civstandard_hook.h_Entry = (HOOKFUNC) Standard_Hook_Func;
}
