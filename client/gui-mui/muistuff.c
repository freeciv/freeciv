#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <libraries/mui.h>
#include <mui/NListview_MCC.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>

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
  STRPTR dest;
  if (!str)
    return 0;
  dest = (STRPTR) AllocVec(strlen(str) + 1, 0);
  strcpy(dest, str);
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
    if (!strcmp(oldtext, text))
      return;
  }

  set(obj, MUIA_Text_Contents, text);
}

VOID vsettextf(Object * obj, STRPTR format, APTR args)
{
  char buf[1024];
  vsprintf(buf, format, args);
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

/*
   Object *MakeImageButton(ULONG image)
   {
   return ImageObject,ImageButtonFrame,
   MUIA_Image_Spec,image,
   MUIA_Background, MUII_ButtonBack,
   MUIA_InputMode, MUIV_InputMode_RelVerify,
   MUIA_Image_FreeVert, TRUE,
   End;

   }
 */

Object *MakeMenu(struct NewMenu * menu)
{
  return MUI_MakeObject(MUIO_MenustripNM, menu, MUIO_MenustripNM_CommandKeyCheck);
}

Object *MakeLabel(STRPTR str)
{
  return (MUI_MakeObject(MUIO_Label, str, 0));
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


struct Hook standart_hook;

/****************************************************************
 Standart Hook function, so we can easily call functions via
 MUIM_CallHook
*****************************************************************/
static void __asm __saveds Standart_Hook_Func(register __a1 ULONG * funcptr)
{
  void (*func) (ULONG *) = (void (*)(ULONG *)) (*funcptr);
  if (func)
    func(funcptr + 1);
}

/****************************************************************
 Initialize the standart hook
*****************************************************************/
void init_standart_hook(void)
{
  standart_hook.h_Entry = (HOOKFUNC) Standart_Hook_Func;
}

