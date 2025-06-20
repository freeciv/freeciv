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

/* SDL3 */
#include <SDL3/SDL.h>

/* utility */
#include "log.h"

/* gui-sdl3 */
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "themespec.h"

#include "widget.h"
#include "widget_p.h"

struct move {
  bool moved;
  struct widget *pwindow;
  float prev_x;
  float prev_y;
};

static int (*baseclass_redraw)(struct widget *pwidget);

/**********************************************************************//**
  Redraw Window Graphic ( without other Widgets )
**************************************************************************/
static int redraw_window(struct widget *pwindow)
{
  int ret;
  SDL_Color title_bg_color = {255, 255, 255, 200};
  SDL_Surface *tmp = NULL;
  SDL_Rect dst = pwindow->size;

  ret = (*baseclass_redraw)(pwindow);
  if (ret != 0) {
    return ret;
  }

  /* Draw theme */
  clear_surface(pwindow->dst->surface, &dst);
  alphablit(pwindow->theme, NULL, pwindow->dst->surface, &dst, 255);

  /* window has title string == has title bar */
  if (pwindow->string_utf8 != NULL) {

    /* Draw Window's TitleBar */
    dst = pwindow->area;
    dst.y -= (WINDOW_TITLE_HEIGHT + 1);
    dst.h = WINDOW_TITLE_HEIGHT;
    fill_rect_alpha(pwindow->dst->surface, &dst, &title_bg_color);

    /* Draw Text on Window's TitleBar */
    tmp = create_text_surf_from_utf8(pwindow->string_utf8);
    dst.x += adj_size(4);
    if (tmp) {
      dst.y += ((WINDOW_TITLE_HEIGHT - tmp->h) / 2);
      alphablit(tmp, NULL, pwindow->dst->surface, &dst, 255);
      FREESURFACE(tmp);
    }

    dst = pwindow->area;

    create_line(pwindow->dst->surface,
                dst.x, dst.y - 1,
                dst.x + dst.w - 1, dst.y - 1,
                get_theme_color(COLOR_THEME_WINDOW_TITLEBAR_SEPARATOR));
  }

  /* Draw frame */
  if (get_wflags(pwindow) & WF_DRAW_FRAME_AROUND_WIDGET) {
    widget_draw_frame(pwindow);
  }

  return 0;
}

/**************************************************************************
  Window mechanism.

  Active Window schould be first on list (All Widgets on this
  Window that are on List must be above)

  LIST:

  *pFirst_Widget_on_Active_Window.

  *pN__Widget_on_Active_Window.
  *pActive_Window. <------
  *pRest_Widgets.

  This trick give us:
  - if any Widget is under ( area of ) this Window and Mouse
    cursor is above them, 'WidgetListScaner(...)' return
    pointer to Active Window not to this Widget.
**************************************************************************/

/**********************************************************************//**
  Set position for the window.
**************************************************************************/
static void window_set_position(struct widget *pwindow, int x, int y)
{
  struct gui_layer *gui_layer;

  pwindow->size.x = 0;
  pwindow->size.y = 0;

  gui_layer = get_gui_layer(pwindow->dst->surface);
  gui_layer->dest_rect.x = x;
  gui_layer->dest_rect.y = y;
}

/**********************************************************************//**
  Selected callback for the window widget.
**************************************************************************/
static void window_select(struct widget *pwindow)
{
  /* Nothing */
}

/**********************************************************************//**
  Unselected callback for the window widget.
**************************************************************************/
static void window_unselect(struct widget *pwindow)
{
  /* Nothing */
}

/**********************************************************************//**
  Set area for the window widget.
**************************************************************************/
static void set_client_area(struct widget *pwindow)
{
  SDL_Rect area;

  if (get_wflags(pwindow) & WF_DRAW_FRAME_AROUND_WIDGET) {
    area.x = current_theme->fr_left->w;
    area.y = current_theme->fr_top->h;
    area.w = pwindow->size.w - current_theme->fr_left->w - current_theme->fr_right->w;
    area.h = pwindow->size.h - current_theme->fr_top->h - current_theme->fr_bottom->h;
  } else {
    area = pwindow->size;
  }

  if (pwindow->string_utf8 != NULL) {
    area.y += (WINDOW_TITLE_HEIGHT + 1);
    area.h -= (WINDOW_TITLE_HEIGHT + 1);
  }

  widget_set_area(pwindow, area);
}

/**********************************************************************//**
  Allocate Window Widget Structure.
  Text to titlebar is taken from 'title'.
**************************************************************************/
struct widget *create_window_skeleton(struct gui_layer *pdest,
                                      utf8_str *title, Uint32 flags)
{
  int w = 0, h = 0;
  struct widget *pwindow = widget_new();

  pwindow->set_position = window_set_position;

  baseclass_redraw = pwindow->redraw;
  pwindow->redraw = redraw_window;
  pwindow->select = window_select;
  pwindow->unselect = window_unselect;

  pwindow->string_utf8 = title;
  set_wflag(pwindow, WF_FREE_STRING | WF_FREE_GFX | WF_FREE_THEME |
            WF_DRAW_FRAME_AROUND_WIDGET| flags);
  set_wstate(pwindow, FC_WS_DISABLED);
  set_wtype(pwindow, WT_WINDOW);
  pwindow->mod = SDL_KMOD_NONE;

  if (get_wflags(pwindow) & WF_DRAW_FRAME_AROUND_WIDGET) {
    w += current_theme->fr_left->w + current_theme->fr_right->w;
    h += current_theme->fr_top->h + current_theme->fr_bottom->h;
  }

  if (title != NULL) {
    SDL_Rect size;

    utf8_str_size(title, &size);

    w += size.w + adj_size(10);
    h += MAX(size.h, WINDOW_TITLE_HEIGHT + 1);
  }

  pwindow->size.w = w;
  pwindow->size.h = h;

  set_client_area(pwindow);

  if (pdest != NULL) {
    pwindow->dst = pdest;
  } else {
    pwindow->dst = add_gui_layer(w, h);
  }

  return pwindow;
}

/**********************************************************************//**
  Create window widget
**************************************************************************/
struct widget *create_window(struct gui_layer *pdest, utf8_str *title,
                             Uint16 w, Uint16 h, Uint32 flags)
{
  struct widget *pwindow = create_window_skeleton(pdest, title, flags);

  resize_window(pwindow, NULL, NULL, w, h);

  return pwindow;
}

/**********************************************************************//**
  Resize Window 'pwindow' to 'new_w' and 'new_h'.
  and refresh window background ( save screen under window ).

  If bcgd == NULL then theme is set to
  white transparent ( ALPHA = 128 ).

  Exp.
  if (resize_window(pwindow, bcgd, new_w, new_h)) {
    FREESURFACE(bcgd);
  }

  @return Was a new surface allocated, which caller should free
**************************************************************************/
bool resize_window(struct widget *pwindow, SDL_Surface *bcgd,
                   SDL_Color *pcolor, Uint16 new_w, Uint16 new_h)
{
  SDL_Color color;

  /* Window */
  if ((new_w != pwindow->size.w) || (new_h != pwindow->size.h)) {
    pwindow->size.w = new_w;
    pwindow->size.h = new_h;

    set_client_area(pwindow);

    if (get_wflags(pwindow) & WF_RESTORE_BACKGROUND) {
      refresh_widget_background(pwindow);
    }

    FREESURFACE(pwindow->dst->surface);
    pwindow->dst->surface = create_surf(pwindow->size.w,
                                        pwindow->size.h);
  }

  if (bcgd != pwindow->theme) {
    FREESURFACE(pwindow->theme);
  }

  if (bcgd) {
    if (bcgd->w != new_w || bcgd->h != new_h) {
      pwindow->theme = resize_surface(bcgd, new_w, new_h, 2);
      return 1;
    } else {
      pwindow->theme = bcgd;
      return FALSE;
    }
  } else {
    pwindow->theme = create_surf(new_w, new_h);

    if (pcolor == NULL) {
      color = *get_theme_color(COLOR_THEME_BACKGROUND);

      pcolor = &color;
    }

    SDL_FillSurfaceRect(pwindow->theme, NULL,
                        map_rgba(pwindow->theme->format, *pcolor));

    return TRUE;
  }
}

/**********************************************************************//**
  Move window as event instructs.
**************************************************************************/
static widget_id move_window_motion(SDL_MouseMotionEvent *motion_event,
                                    void *data)
{
  struct move *pmove = (struct move *)data;
  int xrel, yrel;

  if (!pmove->moved) {
    pmove->moved = TRUE;
  }

  widget_mark_dirty(pmove->pwindow);

  xrel = motion_event->x - pmove->prev_x;
  yrel = motion_event->y - pmove->prev_y;
  pmove->prev_x = motion_event->x;
  pmove->prev_y = motion_event->y;

  widget_set_position(pmove->pwindow,
                      (pmove->pwindow->dst->dest_rect.x + pmove->pwindow->size.x) + xrel,
                      (pmove->pwindow->dst->dest_rect.y + pmove->pwindow->size.y) + yrel);

  widget_mark_dirty(pmove->pwindow);
  flush_dirty();

  return ID_ERROR;
}

/**********************************************************************//**
  Button up event handler for the window moving event loop.
**************************************************************************/
static widget_id move_window_button_up(SDL_MouseButtonEvent *button_event,
                                       void *data)
{
  struct move *pmove = (struct move *)data;

  if (pmove && pmove->moved) {
    return (widget_id)ID_MOVED_WINDOW;
  }

  return (widget_id)ID_WINDOW;
}

/**********************************************************************//**
  Move window in a event loop.
**************************************************************************/
bool move_window(struct widget *pwindow)
{
  bool ret;
  struct move pmove;

  pmove.pwindow = pwindow;
  pmove.moved = FALSE;
  SDL_GetMouseState(&pmove.prev_x, &pmove.prev_y);
  /* Filter mouse motion events */
  SDL_SetEventFilter(FilterMouseMotionEvents, NULL);
  ret = (gui_event_loop((void *)&pmove, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                        move_window_button_up, move_window_motion) == ID_MOVED_WINDOW);
  /* Turn off Filter mouse motion events */
  SDL_SetEventFilter(NULL, NULL);

  return ret;
}
