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

#include <intuition/intuitionbase.h>
#include <libraries/mui.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "objecttreeclass.h"
#include "muistuff.h"

struct ObjectTree_Data
{
  struct ObjectTree_Node *first_node;
  APTR pool;
};

struct ObjectTree_Node
{
  struct MinNode node;
  struct MinList list;
  Object *object;
};

static struct ObjectTree_Node *CreateObjectPooled( APTR pool, Object *o)
{
  struct ObjectTree_Node *node;
  if((node = (struct ObjectTree_Node *)AllocPooled(pool,sizeof(struct ObjectTree_Node))))
  {
    node->object = o;
    NewList((struct List*)&node->list);
  }
  return node;
}

static LONG ObjectTree_CalcHeight(struct ObjectTree_Node *node)
{
  struct ObjectTree_Node *n = (struct ObjectTree_Node *)List_First(&node->list);
  LONG height=0;
  int i;

  if(!n) return _minheight(node->object);

  i = 0;

  while(n)
  {
    i++;
    height += ObjectTree_CalcHeight(n);
    n = (struct ObjectTree_Node *)Node_Next(n);
  }

  if (i)
    height += 4*(i-1);

  return height;
}

static VOID ObjectTree_Layout(struct ObjectTree_Node *node, LONG left, LONG top)
{
  struct ObjectTree_Node *n;
  LONG t;

  if(!node) return;

  n = (struct ObjectTree_Node *)List_First(&node->list);
  t = top;

  MUI_Layout(node->object, left, top, _minwidth(node->object), _minheight(node->object),0);

  while(n)
  {
    ObjectTree_Layout(n,left+_minwidth(node->object)+30,t);
    t += ObjectTree_CalcHeight(n) + 4;
    n = (struct ObjectTree_Node *)Node_Next(n);
  }
}

static VOID ObjectTree_DrawLines(Object *group, struct ObjectTree_Node *node)
{
  struct ObjectTree_Node *n;
  if (!node || !group) return;
  if ((n = (struct ObjectTree_Node *)List_First(&node->list)))
  {
    LONG line_x1 = _right(node->object)+1;
    LONG line_y1 = (_top(node->object) + _bottom(node->object))/2;
    struct RastPort *rp = _rp(group);

    if (n)
    {
      Move(rp,line_x1,line_y1);
      line_x1 = (line_x1 + _left(n->object) - 1)/2;
      Draw(rp,line_x1,line_y1);
    }

    SetAPen(rp,_dri(group)->dri_Pens[TEXTPEN]);

    while(n)
    {
      Object *child = n->object;
      LONG line_x2 = _left(child)-1;
      LONG line_y2 = (_top(child) + _bottom(child))/2;

      Move(rp,line_x1,line_y1);
      Draw(rp,line_x1,line_y2);
      Draw(rp,line_x2,line_y2);

      ObjectTree_DrawLines(group,n);

      n = (struct ObjectTree_Node *)Node_Next(n);
    }
  }
}

static VOID ObjectTree_Clear_Group(APTR pool, Object *group, struct ObjectTree_Node *node)
{
  struct ObjectTree_Node *n;
  if(!node) return;

  if(node->object)
  {
    DoMethod(group,OM_REMMEMBER,node->object);
    MUI_DisposeObject(node->object);
  }

  while((n = (struct ObjectTree_Node *)RemTail((struct List*)&node->list)))
  {
    ObjectTree_Clear_Group(pool,group,n);
    FreePooled(pool,n,sizeof(struct ObjectTree_Node));
  }
}

static APTR ObjectTree_Find(struct ObjectTree_Node *node, Object *o)
{
  struct ObjectTree_Node *n;

  if (!node) return NULL;
  if (node->object == o) return node;

  n = (struct ObjectTree_Node *)List_First(&node->list);

  while (n)
  {
    APTR l = ObjectTree_Find(n,o);
    if (l) return l;
    n = (struct ObjectTree_Node *)Node_Next(n);
  }
  return NULL;
}

HOOKPROTONH(ObjectTree_Group_Layout, ULONG, Object *obj, struct MUI_LayoutMsg *lm)
{
  ULONG ret;
  LONG vert_spacing = 4;
  LONG horiz_spacing = 4;
  Object *cstate = (Object *)lm->lm_Children->mlh_Head;
  Object *child;

  switch (lm->lm_Type)
  {
  case  MUILM_MINMAX:
    {
      WORD maxminwidth  = 0;
      WORD maxminheight = 0;

      /* find out biggest widths & heights of our children */
      while ((child = (Object*)NextObject(&cstate)))
      {
        if (maxminwidth <MUI_MAXMAX && _minwidth (child) > maxminwidth ) maxminwidth  = _minwidth (child);
        if (maxminheight<MUI_MAXMAX && _minheight(child) > maxminheight) maxminheight = _minheight(child);
      }

      /* set the result fields in the message */
      lm->lm_MinMax.MinWidth  = 2*maxminwidth+horiz_spacing;
      lm->lm_MinMax.MinHeight = 2*maxminheight+vert_spacing;
      lm->lm_MinMax.DefWidth  = 4*maxminwidth+3*horiz_spacing;
      lm->lm_MinMax.DefHeight = 4*maxminheight+3*vert_spacing;
      lm->lm_MinMax.MaxWidth  = MUI_MAXMAX;
      lm->lm_MinMax.MaxHeight = MUI_MAXMAX;
    }
    ret = 0;
    break;
  case  MUILM_LAYOUT:
    {
      LONG max_right=0;
      LONG max_bottom=0;

      struct ObjectTree_Node *root_node = ((struct ObjectTree_Data*)INST_DATA(CL_ObjectTree->mcc_Class,obj))->first_node;

       ObjectTree_Layout(root_node, 0,0);

       while ((child = (Object*)NextObject(&cstate)))
       {
         if (_right(child) > max_right) max_right = _right(child);
         if (_bottom(child) > max_bottom) max_bottom = _bottom(child);
       }

       max_right -= _mleft(obj) - 1;
       max_bottom -= _mtop(obj) - 1;

       if(lm->lm_Layout.Width < max_right) lm->lm_Layout.Width = max_right;
       if(lm->lm_Layout.Height < max_bottom) lm->lm_Layout.Height = max_bottom;
    }
    ret = TRUE;
    break;
  default:
    ret = (ULONG) MUILM_UNKNOWN;
  }
  return ret;
}


static ULONG ObjectTree_New(struct IClass *cl, Object * o, struct opSet *msg)
{
  static struct Hook layout_hook = { {0,0}, (HOOKFUNC)ObjectTree_Group_Layout, NULL, NULL };
	
  if ((o = (Object *) DoSuperNew(cl, o,
       MUIA_Group_LayoutHook, &layout_hook,
       TAG_MORE, msg->ops_AttrList)))
  {
    struct ObjectTree_Data *data = (struct ObjectTree_Data *) INST_DATA(cl, o);

    if ((data->pool = CreatePool(0,2048,2048)))
    {
      return (ULONG)o;
    }

    CoerceMethod(cl, o, OM_DISPOSE);
  }
  return 0;
}

static ULONG ObjectTree_Dispose(struct IClass * cl, Object * o, Msg msg)
{
  struct ObjectTree_Data *data = (struct ObjectTree_Data *) INST_DATA(cl, o);
  if (data->pool) DeletePool(data->pool);
  return DoSuperMethodA(cl, o, msg);
}

static ULONG ObjectTree_Draw(struct IClass * cl, Object * o, struct MUIP_Draw *msg)
{
  struct ObjectTree_Data *data = (struct ObjectTree_Data *) INST_DATA(cl, o);
  APTR cliphandle;
  DoSuperMethodA(cl, o, (Msg)msg);

  cliphandle = MUI_AddClipping(muiRenderInfo(o), _mleft(o), _mtop(o), _mwidth(o), _mheight(o));
  ObjectTree_DrawLines(o,data->first_node);
  MUI_RemoveClipping(muiRenderInfo(o), cliphandle);
  return TRUE;
}

static APTR ObjectTree_AddNode(struct IClass *cl, Object *o, struct MUIP_ObjectTree_AddNode *msg)
{
  struct ObjectTree_Data *data = (struct ObjectTree_Data *) INST_DATA(cl, o);
  if (!msg->object) return NULL;
  if (!msg->parent)
  {
    if (data->first_node) return NULL;
    data->first_node = CreateObjectPooled(data->pool,msg->object);
    if (data->first_node) DoMethod(o, OM_ADDMEMBER, data->first_node->object);
    return data->first_node;
  } else
  {
    struct ObjectTree_Node *parent = (struct ObjectTree_Node *)msg->parent;
    struct ObjectTree_Node *node = CreateObjectPooled(data->pool,msg->object);
    if (node)
    {
      AddTail((struct List*)&parent->list,(struct Node*)node);
      DoMethod(o, OM_ADDMEMBER, node->object); 
    }
    return node;
  }
}

static VOID ObjectTree_Clear(struct IClass * cl, Object * o, Msg msg)
{
  struct ObjectTree_Data *data = (struct ObjectTree_Data *) INST_DATA(cl, o);
  ObjectTree_Clear_Group(data->pool,o,data->first_node);
  data->first_node = NULL;
}

static VOID ObjectTree_ClearSubNodes(struct IClass * cl, Object * o, struct MUIP_ObjectTree_ClearSubNodes *msg)
{
  struct ObjectTree_Data *data = (struct ObjectTree_Data *)INST_DATA(cl, o);
  struct ObjectTree_Node *node = (struct ObjectTree_Node *)msg->parent;
  struct ObjectTree_Node *n;

  if(!node) return;

  while((n = (struct ObjectTree_Node *)RemTail((struct List*)&node->list)))
  {
    ObjectTree_Clear_Group(data->pool,o,n);
  }
}

static BOOL ObjectTree_HasSubNodes(/*struct IClass * cl, */Object * o, struct MUIP_ObjectTree_HasSubNodes *msg)
{
  struct ObjectTree_Node *node = (struct ObjectTree_Node *)msg->parent;

  if(!node) return FALSE;

  if(IsListEmpty((struct List*)(&node->list)))
  {
    return FALSE;
  }
  return TRUE;
}

static APTR ObjectTree_FindObject(struct IClass * cl, Object * o, struct MUIP_ObjectTree_FindObject *msg)
{
  struct ObjectTree_Data *data = (struct ObjectTree_Data *) INST_DATA(cl, o);
  return ObjectTree_Find(data->first_node,msg->object);
}

DISPATCHERPROTO(ObjectTree_Dispatcher)
{
  switch (msg->MethodID)
  {
  case OM_NEW:
    return ObjectTree_New(cl, obj, (struct opSet *) msg);
  case OM_DISPOSE:
    return ObjectTree_Dispose(cl, obj, msg);
  case MUIM_Draw:
    return ObjectTree_Draw(cl, obj, (struct MUIP_Draw*)msg);
  case MUIM_ObjectTree_AddNode:
    return (ULONG)ObjectTree_AddNode(cl,obj, (struct MUIP_ObjectTree_AddNode*)msg);
  case MUIM_ObjectTree_Clear:
    ObjectTree_Clear(cl,obj,msg);
    return 0;
  case MUIM_ObjectTree_ClearSubNodes:
    ObjectTree_ClearSubNodes(cl,obj,(struct MUIP_ObjectTree_ClearSubNodes*)msg);
    return 0;
  case MUIM_ObjectTree_FindObject:
    return (ULONG)ObjectTree_FindObject(cl,obj,(struct MUIP_ObjectTree_FindObject*)msg);
  case MUIM_ObjectTree_HasSubNodes:
    return ObjectTree_HasSubNodes(/*cl,*/obj,(struct MUIP_ObjectTree_HasSubNodes*)msg);
  }
  return (DoSuperMethodA(cl, obj, msg));
}

struct MUI_CustomClass *CL_ObjectTree;

BOOL create_objecttree_class(void)
{
  if((CL_ObjectTree =  MUI_CreateCustomClass(NULL, MUIC_Virtgroup, NULL, sizeof(struct ObjectTree_Data), (APTR) ObjectTree_Dispatcher)))
    return TRUE;
  return FALSE;
}

VOID delete_objecttree_class(void)
{
  if (CL_ObjectTree)
    MUI_DeleteCustomClass(CL_ObjectTree);
}
