/***********************************************************************
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
#include <fc_config.h>
#endif

/* SDL2 */
#ifdef SDL2_PLAIN_INCLUDE
#include <SDL.h>
#else  /* SDL2_PLAIN_INCLUDE */
#include <SDL2/SDL.h>
#endif /* SDL2_PLAIN_INCLUDE */

/* gui-sdl2 */
#include "colors.h"
#include "graphics.h"
#include "mapview.h"
#include "themespec.h"
#include "widget.h"
#include "widget_p.h"

/**********************************************************************//**
  Set state of the widget.
**************************************************************************/
void set_wstate(struct widget *pwidget, enum widget_state state)
{
  pwidget->state_types_flags &= ~STATE_MASK;
  pwidget->state_types_flags |= state;
}

/**********************************************************************//**
  Set type of the widget.
**************************************************************************/
void set_wtype(struct widget *pwidget, enum widget_type type)
{
  pwidget->state_types_flags &= ~TYPE_MASK;
  pwidget->state_types_flags |= type;
}

/**********************************************************************//**
  Set flags of the widget.
**************************************************************************/
void set_wflag(struct widget *pwidget, enum widget_flag flag)
{
  (pwidget)->state_types_flags |= ((flag) & FLAG_MASK);
}

/**********************************************************************//**
  Clear flag from the widget.
**************************************************************************/
void clear_wflag(struct widget *pwidget, enum widget_flag flag)
{
  (pwidget)->state_types_flags &= ~((flag) & FLAG_MASK);
}

/**********************************************************************//**
  Get state of the widget.
**************************************************************************/
enum widget_state get_wstate(const struct widget *pwidget)
{
  return ((enum widget_state)(pwidget->state_types_flags & STATE_MASK));
}

/**********************************************************************//**
  Get type of the widget.
**************************************************************************/
enum widget_type get_wtype(const struct widget *pwidget)
{
  return ((enum widget_type)(pwidget->state_types_flags & TYPE_MASK));
}

/**********************************************************************//**
  Get all flags of the widget.
**************************************************************************/
enum widget_flag get_wflags(const struct widget *pwidget)
{
  return ((enum widget_flag)(pwidget->state_types_flags & FLAG_MASK));
}

/**********************************************************************//**
   Free resources allocated for the widget.
**************************************************************************/
void widget_free(struct widget **pwidget)
{
  struct widget *gui = *pwidget;

  if (get_wflags(gui) & WF_FREE_STRING) {
    FREEUTF8STR(gui->string_utf8);
  }
  if (get_wflags(gui) & WF_WIDGET_HAS_INFO_LABEL) {
    FREEUTF8STR(gui->info_label);
  }
  if (get_wflags(gui) & WF_FREE_GFX) {
    FREESURFACE(gui->gfx);
  }
  if (get_wflags(gui) & WF_FREE_THEME) {
    if (get_wtype(gui) == WT_CHECKBOX) {
      FREESURFACE(gui->private_data.cbox->true_theme);
      FREESURFACE(gui->private_data.cbox->false_theme);
    } else {
      FREESURFACE(gui->theme);
    }
  }
  if (get_wflags(gui) & WF_FREE_THEME2) {
    FREESURFACE(gui->theme2);
  }
  if (get_wflags(gui) & WF_FREE_DATA) {
    FC_FREE(gui->data.ptr);
  }
  if (get_wflags(gui) & WF_FREE_PRIVATE_DATA) {
    FC_FREE(gui->private_data.ptr);
  }
  if (NULL != gui->destroy) {
    gui->destroy(gui);
  }

  FC_FREE(*pwidget);
}

/**********************************************************************//**
  Set widget area.
**************************************************************************/
static void widget_core_set_area(struct widget *pwidget, SDL_Rect area)
{
  pwidget->area = area;
}

/**********************************************************************//**
  Set widget position.
**************************************************************************/
static void widget_core_set_position(struct widget *pwidget, int x, int y)
{
  pwidget->size.x = x;
  pwidget->size.y = y;
}

/**********************************************************************//**
  Set widget size.
**************************************************************************/
static void widget_core_resize(struct widget *pwidget, int w, int h)
{
  pwidget->size.w = w;
  pwidget->size.h = h;
}

/**********************************************************************//**
  Draw widget to the surface its on, if it's visible.
**************************************************************************/
static int widget_core_redraw(struct widget *pwidget)
{
  if (!pwidget || (get_wflags(pwidget) & WF_HIDDEN)) {
    return -1;
  }

  if (pwidget->gfx) {
    widget_undraw(pwidget);
  }

  if (!pwidget->gfx && (get_wflags(pwidget) & WF_RESTORE_BACKGROUND)) {
    refresh_widget_background(pwidget);
  }

  return 0;
}

/**********************************************************************//**
  Draw frame of the widget.
**************************************************************************/
static void widget_core_draw_frame(struct widget *pwidget)
{
  draw_frame_inside_widget(pwidget);
}

/**********************************************************************//**
  Mark part of the display covered by the widget dirty.
**************************************************************************/
static void widget_core_mark_dirty(struct widget *pwidget)
{
  SDL_Rect rect = {
    pwidget->dst->dest_rect.x + pwidget->size.x,
    pwidget->dst->dest_rect.y + pwidget->size.y,
    pwidget->size.w,
    pwidget->size.h
  };

  dirty_sdl_rect(&rect);
}

/**********************************************************************//**
  Flush part of the display covered by the widget.
**************************************************************************/
static void widget_core_flush(struct widget *pwidget)
{
  SDL_Rect rect = {
    pwidget->dst->dest_rect.x + pwidget->size.x,
    pwidget->dst->dest_rect.y + pwidget->size.y,
    pwidget->size.w,
    pwidget->size.h
  };

  flush_rect(&rect, FALSE);
}

/**********************************************************************//**
  Clear widget from the display.
**************************************************************************/
static void widget_core_undraw(struct widget *pwidget)
{
  if (get_wflags(pwidget) & WF_RESTORE_BACKGROUND) {
    if (pwidget->gfx) {
      clear_surface(pwidget->dst->surface, &pwidget->size);
      blit_entire_src(pwidget->gfx, pwidget->dst->surface,
                      pwidget->size.x, pwidget->size.y);
    }
  } else {
    clear_surface(pwidget->dst->surface, &pwidget->size);
  }
}

/**********************************************************************//**
  Callback for when widget gets selected.
**************************************************************************/
static void widget_core_select(struct widget *pwidget)
{
  widget_redraw(pwidget);
  widget_flush(pwidget);
}

/**********************************************************************//**
  Callback for when widget gets unselected.
**************************************************************************/
static void widget_core_unselect(struct widget *pwidget)
{
  widget_redraw(pwidget);
  widget_flush(pwidget);
}

/**********************************************************************//**
  Create a new widget.
**************************************************************************/
struct widget *widget_new(void)
{
  struct widget *pwidget = fc_calloc(1, sizeof(struct widget));

  pwidget->set_area = widget_core_set_area;
  pwidget->set_position = widget_core_set_position;
  pwidget->resize = widget_core_resize;
  pwidget->redraw = widget_core_redraw;
  pwidget->draw_frame = widget_core_draw_frame;
  pwidget->mark_dirty = widget_core_mark_dirty;
  pwidget->flush = widget_core_flush;
  pwidget->undraw = widget_core_undraw;
  pwidget->select = widget_core_select;
  pwidget->unselect = widget_core_unselect;

  return pwidget;
}
