/**********************************************************************
 Freeciv - Copyright (C) 2006 - The Freeciv Project
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

#include <SDL/SDL.h>

/* gui-sdl */
#include "colors.h"
#include "graphics.h"
#include "themespec.h"

#include "widget_p.h"
#include "gui_stuff.h"

/**************************************************************************
  ...
**************************************************************************/
inline void set_wstate(struct widget *pWidget, enum widget_state state)
{
  pWidget->state_types_flags &= ~STATE_MASK;
  pWidget->state_types_flags |= state;
}

/**************************************************************************
  ...
**************************************************************************/
inline void set_wtype(struct widget *pWidget, enum widget_type type)
{
  pWidget->state_types_flags &= ~TYPE_MASK;
  pWidget->state_types_flags |= type;
}

/**************************************************************************
  ...
**************************************************************************/
inline void set_wflag(struct widget *pWidget, enum widget_flag flag)
{
  (pWidget)->state_types_flags |= ((flag) & FLAG_MASK);
}

/**************************************************************************
  ...
**************************************************************************/
inline void clear_wflag(struct widget *pWidget, enum widget_flag flag)
{
  (pWidget)->state_types_flags &= ~((flag) & FLAG_MASK);
}

/**************************************************************************
  ...
**************************************************************************/
inline enum widget_state get_wstate(const struct widget *pWidget)
{
  return ((enum widget_state)(pWidget->state_types_flags & STATE_MASK));
}

/**************************************************************************
  ...
**************************************************************************/
inline enum widget_type get_wtype(const struct widget *pWidget)
{
  return ((enum widget_type)(pWidget->state_types_flags & TYPE_MASK));
}

/**************************************************************************
  ...
**************************************************************************/
inline enum widget_flag get_wflags(const struct widget *pWidget)
{
  return ((enum widget_flag)(pWidget->state_types_flags & FLAG_MASK));
}

/**************************************************************************
   ...
**************************************************************************/
void free_widget(struct widget *pGUI)
{
  if ((get_wflags(pGUI) & WF_FREE_STRING) == WF_FREE_STRING) {	\
    FREESTRING16(pGUI->string16);				\
  }								\
  if ((get_wflags(pGUI) & WF_FREE_GFX) == WF_FREE_GFX) {		\
    FREESURFACE(pGUI->gfx);					\
  }								\
  if ((get_wflags(pGUI) & WF_FREE_THEME) == WF_FREE_THEME) {	\
    if (get_wtype(pGUI) == WT_CHECKBOX) {			\
      FREESURFACE(pGUI->private_data.cbox->pTRUE_Theme);		\
      FREESURFACE(pGUI->private_data.cbox->pFALSE_Theme);		\
    } else {							\
      FREESURFACE(pGUI->theme);				\
    }								\
  }								\
  if ((get_wflags(pGUI) & WF_FREE_DATA) == WF_FREE_DATA) {	\
    FC_FREE(pGUI->data.ptr);					\
  }								\
  if ((get_wflags(pGUI) & WF_FREE_PRIVATE_DATA) == WF_FREE_PRIVATE_DATA) { 	\
    FC_FREE(pGUI->private_data.ptr);				\
  }								\
}
