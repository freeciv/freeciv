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
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>

#include "colortextclass.h"
#include "muistuff.h"

struct ColorText_Data
{
  LONG fgpen;
  LONG bgpen;

  LONG fgcol;
  LONG bgcol;
  LONG ownbg;

  STRPTR contents;
  LONG align;

  LONG innerleft,innertop,innerright,innerbottom;
  LONG setup; /* TRUE if between MUIM_Setup and MUIM_Cleanup */
};


static ULONG ColorText_New(struct IClass *cl, Object * o, struct opSet *msg)
{
  if ((o = (Object *) DoSuperNew(cl, o,
  				 MUIA_InnerLeft,0,
  				 MUIA_InnerTop,0,
  				 MUIA_InnerRight,0,
  				 MUIA_InnerBottom,0,
				 TAG_MORE, msg->ops_AttrList)))
  {
    struct ColorText_Data *data = (struct ColorText_Data *) INST_DATA(cl, o);
    struct TagItem *tl = msg->ops_AttrList;
    struct TagItem *ti;

    data->fgpen = -1;
    data->bgpen = -1;

    while ((ti = NextTagItem(&tl)))
    {
      switch (ti->ti_Tag)
      {
	case  MUIA_ColorText_Contents:
	      data->contents = StrCopy((STRPTR)ti->ti_Data);
      	      break;

        case  MUIA_ColorText_Background:
              data->bgcol = ti->ti_Data;
              data->ownbg = TRUE;
	      break;

	case  MUIA_ColorText_Align:
	      data->align = ti->ti_Data;
	      break;
      }
    }

    if (data->ownbg) set(o,MUIA_FillArea,FALSE);
  }
  return (ULONG) o;
}

static ULONG ColorText_Dispose(struct IClass * cl, Object * o, Msg msg)
{
  struct ColorText_Data *data = (struct ColorText_Data *) INST_DATA(cl, o);
  if (data->contents) FreeVec(data->contents);
  return DoSuperMethodA(cl, o, msg);
}

static ULONG ColorText_Set(struct IClass *cl, Object * o, struct opSet *msg)
{
  struct ColorText_Data *data = (struct ColorText_Data *) INST_DATA(cl, o);
  struct TagItem *tl = msg->ops_AttrList;
  struct TagItem *ti;
  BOOL redraw = FALSE;
  BOOL pen_changed = FALSE;

  while ((ti = NextTagItem(&tl)))
  {
      switch (ti->ti_Tag)
      {
	case  MUIA_ColorText_Contents:
	      if(data->contents) FreeVec(data->contents);
	      data->contents = StrCopy((STRPTR)ti->ti_Data);
	      redraw = TRUE;
      	      break;

        case  MUIA_ColorText_Background:
              if (data->bgcol != ti->ti_Data)
              {
	        redraw = pen_changed = TRUE;
	        data->bgcol = ti->ti_Data;
	        data->ownbg = TRUE;
              }
	      break;

	case  MUIA_ColorText_Align:
	      if (data->align != ti->ti_Data)
	      {
	      	data->align = ti->ti_Data;
	      	redraw = TRUE;
	      }
	      break;
      }
  }

  if (redraw && data->setup)
  {
    struct ColorMap *cm;
    LONG old_pen;

    cm = _screen(o)->ViewPort.ColorMap;
    old_pen = data->bgpen;
    if (pen_changed)
    {
      data->bgpen = ObtainBestPenA(cm,
             MAKECOLOR32(((data->bgcol >> 16)&0xff)),
             MAKECOLOR32(((data->bgcol >> 8)&0xff)),
             MAKECOLOR32((data->bgcol & 0xff)), NULL);
    }
    MUI_Redraw(o, MADF_DRAWOBJECT);
    if (old_pen != -1 && pen_changed) ReleasePen(cm,old_pen);
  }
  return DoSuperMethodA(cl,o,(Msg)msg);
}

static ULONG ColorText_Get(struct IClass * cl, Object * o, struct opGet * msg)
{
  struct ColorText_Data *data = (struct ColorText_Data *) INST_DATA(cl, o);
  switch (msg->opg_AttrID)
  {
    case  MUIA_ColorText_Contents:
          *msg->opg_Storage = (ULONG)data->contents;
          break;

    default:
          return DoSuperMethodA(cl, o, (Msg) msg);
  }
  return 1;
}


static ULONG ColorText_Setup(struct IClass * cl, Object * o, Msg msg)
{
  struct ColorText_Data *data = (struct ColorText_Data *) INST_DATA(cl, o);
  struct ColorMap *cm;

  if (!DoSuperMethodA(cl, o, msg))
    return FALSE;

  cm = _screen(o)->ViewPort.ColorMap;

  if (data->ownbg)
  {
    data->bgpen = ObtainBestPenA(cm,
                                 MAKECOLOR32(((data->bgcol >> 16)&0xff)),
                                 MAKECOLOR32(((data->bgcol >> 8)&0xff)),
                                 MAKECOLOR32((data->bgcol & 0xff)), NULL);
  } else data->bgpen = -1;

  data->innerleft=2;
  data->innertop=1;
  data->innerright=2;
  data->innerbottom=1;
  data->setup = TRUE;

  return TRUE;
}

static ULONG ColorText_Cleanup(struct IClass * cl, Object * o, Msg msg)
{
  struct ColorText_Data *data = (struct ColorText_Data *) INST_DATA(cl, o);
  struct ColorMap *cm;

  data->setup = FALSE;

  cm = _screen(o)->ViewPort.ColorMap;

  if (data->bgpen != -1) ReleasePen(cm, data->bgpen);

  return DoSuperMethodA(cl, o, msg);
}

static ULONG ColorText_AskMinMax(struct IClass * cl, Object * o, struct MUIP_AskMinMax * msg)
{
  LONG width,height;
  struct RastPort rp;
  struct ColorText_Data *data = (struct ColorText_Data *) INST_DATA(cl, o);
  DoSuperMethodA(cl, o, (Msg) msg);

  InitRastPort(&rp);
  SetFont(&rp,_font(o));

  if(data->contents)
    width = TextLength(&rp,data->contents, strlen(data->contents)) + data->innerleft + data->innerright;
  else
    width = 10; /* something higher than 0 */

  height = _font(o)->tf_YSize + data->innertop + data->innerbottom;;

  msg->MinMaxInfo->MinWidth += width;
  msg->MinMaxInfo->DefWidth += width;
  msg->MinMaxInfo->MaxWidth += MUI_MAXMAX;

  msg->MinMaxInfo->MinHeight += height;
  msg->MinMaxInfo->DefHeight += height;
  msg->MinMaxInfo->MaxHeight += height;
  return 0;
}

static ULONG ColorText_Draw(struct IClass * cl, Object * o, struct MUIP_Draw * msg)
{
  struct ColorText_Data *data = (struct ColorText_Data *) INST_DATA(cl, o);

  DoSuperMethodA(cl,o,(Msg)msg);

  if (data->bgpen != -1)
  {
    SetAPen(_rp(o), data->bgpen);
    RectFill(_rp(o),_mleft(o), _mtop(o), _mright(o), _mbottom(o));
  }

  if (data->contents)
  {
    struct TextExtent te;
    LONG x_off;
    LONG count = strlen(data->contents);
    LONG w = _mwidth(o) - data->innerleft - data->innerright;
    LONG h = _mheight(o) - data->innertop - data->innerbottom;

    count = TextFit(_rp(o),data->contents,count,&te, NULL, 1, w, h);

    if (data->align == MUIV_ColorText_Align_Left) x_off = 0;
    else x_off = (w - te.te_Width)/2;

    SetAPen(_rp(o), _pens(o)[MPEN_TEXT]);
    Move(_rp(o),_mleft(o) + data->innerleft + x_off,
         _mtop(o) + _font(o)->tf_Baseline + data->innertop);
    Text(_rp(o),data->contents, count);
  }
  return 0;
}

DISPATCHERPROTO(ColorText_Dispatcher)
{
  switch (msg->MethodID)
  {
  case OM_NEW:
    return ColorText_New(cl, obj, (struct opSet *) msg);
  case OM_DISPOSE:
    return ColorText_Dispose(cl, obj, msg);
  case OM_GET:
    return ColorText_Get(cl, obj, (struct opGet *) msg);
  case OM_SET:
    return ColorText_Set(cl, obj, (struct opSet *) msg);
  case MUIM_Setup:
    return ColorText_Setup(cl, obj, msg);
  case MUIM_Cleanup:
    return ColorText_Cleanup(cl, obj, msg);
  case MUIM_AskMinMax:
    return ColorText_AskMinMax(cl, obj, (struct MUIP_AskMinMax *) msg);
  case MUIM_Draw:
    return ColorText_Draw(cl, obj, (struct MUIP_Draw *) msg);
  }
  return (DoSuperMethodA(cl, obj, msg));
}

struct MUI_CustomClass *CL_ColorText;

BOOL create_colortext_class(void)
{
  if ((CL_ColorText = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct ColorText_Data), (APTR) ColorText_Dispatcher)))
      return TRUE;
  return FALSE;
}

VOID delete_colortext_class(void)
{
  if (CL_ColorText)
    MUI_DeleteCustomClass(CL_ColorText);
}
