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

#include "muistuff.h"
#include "autogroupclass.h"

struct AutoGroup_Data
{
  ULONG defvobjs;
};

HOOKPROTONH(AutoGroup_Layout, ULONG, Object *obj, struct MUI_LayoutMsg *lm)
{
  LONG vert_spacing = 4;
  LONG horiz_spacing = 4;

  switch (lm->lm_Type)
  {
    case  MUILM_MINMAX:
          {
	    /*
	     ** MinMax calculation function. When this is called,
	     ** the children of your group have already been asked
	     ** about their min/max dimension so you can use their
	     ** dimensions to calculate yours.
	     **
	     ** In this example, we make our minimum size twice as
	     ** big as the biggest child in our group.
	     */

            Object *cstate = (Object *)lm->lm_Children->mlh_Head;
	    Object *child;
	    struct AutoGroup_Data *data = INST_DATA(CL_AutoGroup->mcc_Class,obj);

	    WORD maxminwidth  = 0;
	    WORD maxminheight = 0;
	    ULONG defvobjs = data->defvobjs;

	    /* find out biggest widths & heights of our children */

	    while (child = NextObject(&cstate))
	    {
	      if (maxminwidth <MUI_MAXMAX && _minwidth (child) > maxminwidth ) maxminwidth  = _minwidth (child);
	      if (maxminheight<MUI_MAXMAX && _minheight(child) > maxminheight) maxminheight = _minheight(child);
            }

	    /* set the result fields in the message */

	    lm->lm_MinMax.MinWidth  = 2*maxminwidth+horiz_spacing;
	    lm->lm_MinMax.MinHeight = 2*maxminheight+vert_spacing;
	    lm->lm_MinMax.DefWidth  = 4*maxminwidth+3*horiz_spacing;
	    lm->lm_MinMax.DefHeight = defvobjs*maxminheight+(defvobjs-1)*vert_spacing;
	    lm->lm_MinMax.MaxWidth  = MUI_MAXMAX;
	    lm->lm_MinMax.MaxHeight = MUI_MAXMAX;

	    return(0);
	  }

	  case  MUILM_LAYOUT:
		{
		  /*
		  ** Layout function. Here, we have to call MUI_Layout() for each
		  ** our children. MUI wants us to place them in a rectangle
		  ** defined by (0,0,lm->lm_Layout.Width-1,lm->lm_Layout.Height-1)
		  ** You are free to put the children anywhere in this rectangle.
		  **
		  ** If you are a virtual group, you may also extend
		  ** the given dimensions and place your children anywhere. Be sure
		  ** to return the dimensions you need in lm->lm_Layout.Width and
		  ** lm->lm_Layout.Height in this case.
		  **
		  ** Return TRUE if everything went ok, FALSE on error.
		  ** Note: Errors during layout are not easy to handle for MUI.
		  **       Better avoid them!
		  */

		  Object *cstate = (Object *)lm->lm_Children->mlh_Head;
		  Object *child;
		  LONG l = 0;
		  LONG t = 0;
		  LONG maxwidth = 0;
		  LONG height = 0;
		  BOOL first = TRUE;

		  while (child = NextObject(&cstate))
		  {
		    LONG mw = _minwidth (child);
		    LONG mh = _minheight(child);

		    if (!first)
		    {
		      if (t + mh + vert_spacing > lm->lm_Layout.Height)
		      {
		      	if (t > height) height = t;
			l += maxwidth + horiz_spacing;
			t = 0;
			maxwidth = 0;
		      } else t += vert_spacing;
		    } else first = FALSE;

		    if (mw > maxwidth) maxwidth = mw;

		    if (!MUI_Layout(child,l,t,mw,mh,0))
		      return FALSE;

		    t += mh;
		  }

		  l += maxwidth;

		  if(lm->lm_Layout.Width < l) lm->lm_Layout.Width = l;
		  if(lm->lm_Layout.Height < height) lm->lm_Layout.Height = height;

		  return TRUE;
                }
  }
  return MUILM_UNKNOWN;
}

static ULONG AutoGroup_New(struct IClass *cl, Object * o, struct opSet *msg)
{
  static struct Hook layout_hook = { {0,0}, (HOOKFUNC)AutoGroup_Layout, NULL, NULL }; 
	
  if ((o = (Object *) DoSuperNew(cl, o,
       MUIA_Group_LayoutHook, &layout_hook,
       TAG_MORE, msg->ops_AttrList)))
  {
    struct AutoGroup_Data *data = (struct AutoGroup_Data*)INST_DATA(cl,o);
    struct TagItem *ti;

    if ((ti = FindTagItem(MUIA_AutoGroup_DefVertObjects,msg->ops_AttrList)))
    {
      data->defvobjs = ti->ti_Data;
    } else data->defvobjs = 4;
    return (ULONG)o;
  }
  return 0;
}

static ULONG AutoGroup_Get(struct IClass *cl, Object *o, struct opGet *msg)
{
  switch (msg->opg_AttrID)
  {
    case  MUIA_AutoGroup_NumObjects:
    	  {
    	    int i = 0;
	    struct List *child_list = (struct List*)xget(o,MUIA_Group_ChildList);
	    Object *cstate = (Object *)child_list->lh_Head;
	    Object *child;

	    while ((child = (Object*)NextObject(&cstate)))
	      i++;

    	    *msg->opg_Storage = i;
    	  }
	  break;

    default:
          return DoSuperMethodA(cl, o, (Msg) msg);
  }
  return 1;
}

static VOID AutoGroup_DisposeChilds(/*struct IClass *cl,*/ Object *o/*, Msg msg*/)
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

DISPATCHERPROTO(AutoGroup_Dispatcher)
{
  switch (msg->MethodID)
  {
    case OM_NEW: return AutoGroup_New(cl, obj, (struct opSet *) msg);
    case OM_GET: return AutoGroup_Get(cl, obj, (struct opGet *) msg);
    case MUIM_AutoGroup_DisposeChilds: AutoGroup_DisposeChilds(/*cl, */obj/*, msg*/);return 0;
  }
  return (DoSuperMethodA(cl, obj, msg));
}


struct MUI_CustomClass *CL_AutoGroup;

BOOL create_autogroup_class(void)
{
  if((CL_AutoGroup =  MUI_CreateCustomClass(NULL, MUIC_Virtgroup, NULL, sizeof(struct AutoGroup_Data), (APTR) AutoGroup_Dispatcher)))
    return TRUE;
  return FALSE;
}

VOID delete_autogroup_class(void)
{
  if (CL_AutoGroup)
    MUI_DeleteCustomClass(CL_AutoGroup);
}
