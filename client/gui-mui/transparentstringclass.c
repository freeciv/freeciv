/********************************************************************** 
 Freeciv - Copyright(C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or(at your option)
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

#include <intuition/intuitionbase.h>
#include <libraries/mui.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/locale.h>

#include "support.h"
#include "transparentstringclass.h"
#include "muistuff.h"

/* Undocumented */
#define MUIM_GoActive   0x8042491a
#define MUIM_GoInactive 0x80422c0c

/* The size of the edit buffer */
#define BUF_SIZE 128

struct TransparentString_Data {
  struct MUI_EventHandlerNode ehnode;

  UBYTE edit_buffer[BUF_SIZE + 1];
  LONG cursor_pos;

  struct Locale *locale;
};

static ULONG TransparentString_New(struct IClass * cl, Object * o, struct opSet *msg)
{
  if ((o = (Object *) DoSuperNew(cl, o,
				 MUIA_CycleChain, 1,
				 TAG_MORE, msg->ops_AttrList))) {
    struct TransparentString_Data *data = (struct TransparentString_Data *) INST_DATA(cl, o);
    if ((data->locale = OpenLocale(NULL))) {
      return (ULONG) o;
    }
    CoerceMethod(cl, (Object *) o, OM_DISPOSE);
  }
  return 0;
}

static ULONG TransparentString_Dispose(struct IClass * cl, Object * o, Msg msg)
{

  return DoSuperMethodA(cl, o, msg);
}

static ULONG TransparentString_Get(struct IClass * cl, Object * o, struct opGet * msg)
{
  struct TransparentString_Data *data = (struct TransparentString_Data *) INST_DATA(cl, o);
  switch (msg->opg_AttrID) {
  case MUIA_TransparentString_Contents:
    *msg->opg_Storage = (LONG) data->edit_buffer;
    return TRUE;

  default:
    return DoSuperMethodA(cl, o, (Msg) msg);
  }
}

static ULONG TransparentString_Set(struct IClass * cl, Object * o, struct opSet * msg)
{
  struct TransparentString_Data *data = (struct TransparentString_Data *) INST_DATA(cl, o);
  struct TagItem *tl = msg->ops_AttrList;
  struct TagItem *ti;

  while ((ti = NextTagItem(&tl))) {
    switch (ti->ti_Tag) {
    case MUIA_TransparentString_Contents:
      if (ti->ti_Data) {
	sz_strlcpy(data->edit_buffer, (char *) ti->ti_Data);
      } else {
	data->edit_buffer[0] = 0;
	data->cursor_pos = 0;
      }
      MUI_Redraw(o, MADF_DRAWOBJECT);
      break;
    }
  }
  return DoSuperMethodA(cl, o, (Msg) msg);
}

static ULONG TransparentString_AskMinMax(struct IClass * cl, Object * o, struct MUIP_AskMinMax * msg)
{
  DoSuperMethodA(cl, o, (Msg) msg);

  msg->MinMaxInfo->MinWidth += _font(o)->tf_XSize * 4;
  msg->MinMaxInfo->DefWidth += 200;
  msg->MinMaxInfo->MaxWidth += MUI_MAXMAX;

  msg->MinMaxInfo->MinHeight += _font(o)->tf_YSize;
  msg->MinMaxInfo->DefHeight += _font(o)->tf_YSize;
  msg->MinMaxInfo->MaxHeight += _font(o)->tf_YSize;
  return 0;
}

static ULONG TransparentString_Setup(struct IClass * cl, Object * o, Msg msg)
{
  struct TransparentString_Data *data = (struct TransparentString_Data *) INST_DATA(cl, o);

  if (!DoSuperMethodA(cl, o, msg))
    return FALSE;

  data->ehnode.ehn_Object = o;
  data->ehnode.ehn_Class = cl;
  data->ehnode.ehn_Events = IDCMP_MOUSEBUTTONS;
  DoMethod(_win(o), MUIM_Window_AddEventHandler, &data->ehnode);

  return TRUE;
}

static ULONG TransparentString_Cleanup(struct IClass * cl, Object * o, Msg msg)
{
  struct TransparentString_Data *data = (struct TransparentString_Data *) INST_DATA(cl, o);
  DoMethod(_win(o), MUIM_Window_RemEventHandler, &data->ehnode);
  DoSuperMethodA(cl, o, msg);
  return 0;
}

static VOID TransparentString_Draw(struct IClass * cl, Object * o, struct MUIP_Draw * msg)
{
  struct TransparentString_Data *data = (struct TransparentString_Data *) INST_DATA(cl, o);
  UBYTE *buf = data->edit_buffer;
  LONG twidth;
  LONG cwidth;
  LONG x;
  LONG edit_len = strlen(buf);

  DoSuperMethodA(cl, o, (Msg) msg);

  SetFont(_rp(o), _font(o));

  twidth = TextLength(_rp(o), data->edit_buffer, edit_len);
  x = (_mwidth(o) - twidth) / 2;

  twidth = TextLength(_rp(o), data->edit_buffer, data->cursor_pos);
  if (buf[data->cursor_pos])
    cwidth = TextLength(_rp(o), &buf[data->cursor_pos], 1);
  else
    cwidth = _font(o)->tf_XSize;

  if ((Object *) xget(_win(o), MUIA_Window_ActiveObject) == o) {
    SetAPen(_rp(o), _dri(o)->dri_Pens[FILLPEN]);
    RectFill(_rp(o), _mleft(o) + x + twidth, _mtop(o), _mleft(o) + x + twidth + cwidth - 1, _mbottom(o));

    SetDrMd(_rp(o), JAM1);
    SetAPen(_rp(o), _dri(o)->dri_Pens[TEXTPEN]);
    Move(_rp(o), _mleft(o) + x, _mtop(o) + _font(o)->tf_Baseline);
    Text(_rp(o), buf, data->cursor_pos);

    if (data->cursor_pos < edit_len) {
      SetAPen(_rp(o), _dri(o)->dri_Pens[FILLTEXTPEN]);
      Text(_rp(o), &buf[data->cursor_pos], 1);
      SetAPen(_rp(o), _dri(o)->dri_Pens[TEXTPEN]);
      Text(_rp(o), &buf[data->cursor_pos + 1], edit_len - data->cursor_pos - 1);
    }
  } else {
    SetDrMd(_rp(o), JAM1);
    SetAPen(_rp(o), _dri(o)->dri_Pens[TEXTPEN]);
    Move(_rp(o), _mleft(o) + x, _mtop(o) + _font(o)->tf_Baseline);
    Text(_rp(o), buf, edit_len);
  }
}

static ULONG TransparentString_HandleEvent(struct IClass *cl, Object * o, struct MUIP_HandleEvent *msg)
{
  struct TransparentString_Data *data = (struct TransparentString_Data *) INST_DATA(cl, o);
  ULONG retval = 0;
  BOOL redraw = FALSE;

  if (msg) {
    ULONG cl = msg->imsg->Class;
    UWORD code = msg->imsg->Code;
    UWORD qual = msg->imsg->Qualifier;
    WORD x = msg->imsg->MouseX;
    WORD y = msg->imsg->MouseY;

    switch (cl) {
    case IDCMP_MOUSEBUTTONS:
      if (code == SELECTDOWN) {
	if (_isinobject(x, y)) {
	  /* Find out the character over the mouse */

	  struct RastPort rp;
	  struct TextExtent te;
	  LONG twidth, cnt;
	  LONG rectwidth;

	  InitRastPort(&rp);
	  SetFont(&rp, _font(o));

	  twidth = TextLength(&rp, data->edit_buffer, strlen(data->edit_buffer));
	  rectwidth = x - _mleft(o) - (_mwidth(o) - twidth) / 2;

	  if (rectwidth > 0)
	    cnt = TextFit(&rp, data->edit_buffer, strlen(data->edit_buffer), &te, NULL, 1, rectwidth, _font(o)->tf_YSize);
	  else
	    cnt = 0;

	  data->cursor_pos = cnt;

	  /* if the object is already the active one redraw it */
	  if ((Object *) xget(_win(o), MUIA_Window_ActiveObject) == o)
	    redraw = 1;
	  else
	    set(_win(o), MUIA_Window_ActiveObject, o);
	} else {
	  set(_win(o), MUIA_Window_ActiveObject, NULL);
	}
      }
      break;

    case IDCMP_VANILLAKEY:
      {
	switch (code) {
	case 8:		/* backspace */
	  if (data->cursor_pos) {
	    LONG shift;
	    LONG i;

	    if ((qual & IEQUALIFIER_LSHIFT) || (qual & IEQUALIFIER_RSHIFT)) {
	      shift = data->cursor_pos;
	    } else
	      shift = 1;

	    i = data->cursor_pos = data->cursor_pos - shift;
	    while (data->edit_buffer[i + shift]) {
	      data->edit_buffer[i] = data->edit_buffer[i + shift];
	      i++;
	    }
	    data->edit_buffer[i] = 0;
	    redraw = 1;
	  }
	  break;

	case 127:		/* del */
	  {
	    if ((qual & IEQUALIFIER_LSHIFT) || (qual & IEQUALIFIER_RSHIFT)) {
	      data->edit_buffer[data->cursor_pos] = 0;
	    } else {
	      LONG i = data->cursor_pos;
	      while (data->edit_buffer[i]) {
		data->edit_buffer[i] = data->edit_buffer[i + 1];
		i++;
	      }
	    }
	    redraw = 1;
	  }
	  break;

	case 24:		/* CTRL X */
	  data->edit_buffer[0] = 0;
	  data->cursor_pos = 0;
	  redraw = 1;
	  break;

	case 13:		/* return */
	  set(_win(o), MUIA_Window_ActiveObject, NULL);
	  set(o, MUIA_TransparentString_Acknowledge, TRUE);
	  break;

	default:
	  if (!IsCntrl(data->locale, code)) {
	    LONG buf_len = strlen(data->edit_buffer);
	    LONG i;
	    if (buf_len < BUF_SIZE) {
	      for (i = buf_len; i >= data->cursor_pos; i--)
		data->edit_buffer[i + 1] = data->edit_buffer[i];

	      data->edit_buffer[data->cursor_pos++] = code;
	      data->edit_buffer[buf_len + 1] = 0;
	      redraw = 1;
	    }
	  }
	}
      }
      break;
    }
  }
  switch (msg->muikey) {
  case MUIKEY_LEFT:
    if (data->cursor_pos) {
      data->cursor_pos--;
      redraw = TRUE;
    }
    break;

  case MUIKEY_RIGHT:
    if (data->cursor_pos < strlen(data->edit_buffer)) {
      data->cursor_pos++;
      redraw = TRUE;
    }
    break;

  case MUIKEY_LINESTART:
    data->cursor_pos = 0;
    redraw = TRUE;
    break;

  case MUIKEY_LINEEND:
    data->cursor_pos = strlen(data->edit_buffer);
    redraw = TRUE;
    break;
  }

  if (redraw)
    MUI_Redraw(o, MADF_DRAWOBJECT);

  return retval;
}

static ULONG TransparentString_GoActive(struct IClass * cl, Object * o)
{
  struct TransparentString_Data *data = (struct TransparentString_Data *) INST_DATA(cl, o);

  DoMethod(_win(o), MUIM_Window_RemEventHandler, &data->ehnode);
  data->ehnode.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_VANILLAKEY | IDCMP_RAWKEY;
  DoMethod(_win(o), MUIM_Window_AddEventHandler, &data->ehnode);

  MUI_Redraw(o, MADF_DRAWOBJECT);
  return 0;
}

static ULONG TransparentString_GoInactive(struct IClass * cl, Object * o)
{
  struct TransparentString_Data *data = (struct TransparentString_Data *) INST_DATA(cl, o);

  DoMethod(_win(o), MUIM_Window_RemEventHandler, &data->ehnode);
  data->ehnode.ehn_Events = IDCMP_MOUSEBUTTONS;
  DoMethod(_win(o), MUIM_Window_AddEventHandler, &data->ehnode);

  MUI_Redraw(o, MADF_DRAWOBJECT);
  return 0;
}

DISPATCHERPROTO(TransparentString_Dispatcher)
{
  switch (msg->MethodID) {
  case OM_NEW:
    return TransparentString_New(cl, obj, (struct opSet *) msg);

  case OM_DISPOSE:
    return TransparentString_Dispose(cl, obj, msg);

  case OM_GET:
    return TransparentString_Get(cl, obj, (struct opGet *) msg);

  case OM_SET:
    return TransparentString_Set(cl, obj, (struct opSet *) msg);

  case MUIM_AskMinMax:
    return TransparentString_AskMinMax(cl, obj, (struct MUIP_AskMinMax *) msg);

  case MUIM_Setup:
    return TransparentString_Setup(cl, obj, msg);

  case MUIM_Cleanup:
    return TransparentString_Cleanup(cl, obj, msg);

  case MUIM_GoActive:
    return TransparentString_GoActive(cl, obj);

  case MUIM_GoInactive:
    return TransparentString_GoInactive(cl, obj);

  case MUIM_HandleEvent:
    return TransparentString_HandleEvent(cl, obj, (struct MUIP_HandleEvent *) msg);

  case MUIM_Draw:
    TransparentString_Draw(cl, obj, (struct MUIP_Draw *) msg);
    return 0;

  }
  return DoSuperMethodA(cl, obj, msg);
}

struct MUI_CustomClass *CL_TransparentString;

int create_transparentstring_class(void)
{
  if ((CL_TransparentString = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct TransparentString_Data), (APTR) TransparentString_Dispatcher)))
     return TRUE;
  return FALSE;
}

void delete_transparentstring_class(void)
{
  if (CL_TransparentString) {
    MUI_DeleteCustomClass(CL_TransparentString);
    CL_TransparentString = NULL;
  }
}
