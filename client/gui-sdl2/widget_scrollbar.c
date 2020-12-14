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

/* utility */
#include "log.h"

/* gui-sdl2 */
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "themespec.h"

#include "widget.h"
#include "widget_p.h"

struct UP_DOWN {
  struct widget *begin;
  struct widget *end;
  struct widget *begin_widget_list;
  struct widget *end_widget_list;
  struct scroll_bar *vscroll;
  float old_y;
  int step;
  int prev_x;
  int prev_y;
  int offset; /* number of pixels the mouse is away from the slider origin */
};

#define widget_add_next(new_widget, add_dock)	\
do {						\
  new_widget->prev = add_dock;                  \
  new_widget->next = add_dock->next;		\
  if (add_dock->next) {                         \
    add_dock->next->prev = new_widget;          \
  }						\
  add_dock->next = new_widget;                  \
} while (FALSE)

static int (*baseclass_redraw)(struct widget *pwidget);

/* =================================================== */
/* ===================== VSCROOLBAR ================== */
/* =================================================== */

/**********************************************************************//**
  Create background image for vscrollbars
  then return pointer to this image.

  Graphic is taken from pVert_theme surface and blit to new created image.

  height depend of 'High' parameter.
**************************************************************************/
static SDL_Surface *create_vertical_surface(SDL_Surface *pVert_theme,
                                            enum widget_state state, Uint16 High)
{
  SDL_Surface *pVerSurf = NULL;
  SDL_Rect src, des;
  Uint16 i;
  Uint16 start_y;
  Uint16 tile_count_midd;
  Uint8 tile_len_end;
  Uint8 tile_len_midd;

  tile_len_end = pVert_theme->h / 16;

  start_y = 0 + state * (pVert_theme->h / 4);

  tile_len_midd = pVert_theme->h / 4 - tile_len_end * 2;

  tile_count_midd = (High - tile_len_end * 2) / tile_len_midd;

  /* correction */
  if (tile_len_midd * tile_count_midd + tile_len_end * 2 < High) {
    tile_count_midd++;
  }

  if (!tile_count_midd) {
    pVerSurf = create_surf(pVert_theme->w, tile_len_end * 2, SDL_SWSURFACE);
  } else {
    pVerSurf = create_surf(pVert_theme->w, High, SDL_SWSURFACE);
  }

  src.x = 0;
  src.y = start_y;
  src.w = pVert_theme->w;
  src.h = tile_len_end;
  alphablit(pVert_theme, &src, pVerSurf, NULL, 255);

  src.y = start_y + tile_len_end;
  src.h = tile_len_midd;

  des.x = 0;

  for (i = 0; i < tile_count_midd; i++) {
    des.y = tile_len_end + i * tile_len_midd;
    alphablit(pVert_theme, &src, pVerSurf, &des, 255);
  }

  src.y = start_y + tile_len_end + tile_len_midd;
  src.h = tile_len_end;
  des.y = pVerSurf->h - tile_len_end;
  alphablit(pVert_theme, &src, pVerSurf, &des, 255);

  return pVerSurf;
}

/**********************************************************************//**
  Blit vertical scrollbar gfx to surface its on.
**************************************************************************/
static int redraw_vert(struct widget *pVert)
{
  int ret;
  SDL_Rect dest = pVert->size;
  SDL_Surface *pVert_Surf;

  ret = (*baseclass_redraw)(pVert);
  if (ret != 0) {
    return ret;
  }
  
  pVert_Surf = create_vertical_surface(pVert->theme,
                                       get_wstate(pVert),
                                       pVert->size.h);
  ret =
      blit_entire_src(pVert_Surf, pVert->dst->surface, dest.x, dest.y);

  FREESURFACE(pVert_Surf);

  return ret;
}

/**********************************************************************//**
  Create ( malloc ) vertical scroll_bar Widget structure.

  Theme graphic is taken from vert_theme surface;

  This function determinate future size of vertical scroll_bar
  ( width = 'vert_theme->w', height = 'height' ) and
  save this in: pwidget->size rectangle ( SDL_Rect )

  Return pointer to created Widget.
**************************************************************************/
struct widget *create_vertical(SDL_Surface *vert_theme, struct gui_layer *pdest,
                               Uint16 height, Uint32 flags)
{
  struct widget *vert = widget_new();

  vert->theme = vert_theme;
  vert->size.w = vert_theme->w;
  vert->size.h = height;
  set_wflag(vert, (WF_FREE_STRING | WF_FREE_GFX | flags));
  set_wstate(vert, FC_WS_DISABLED);
  set_wtype(vert, WT_VSCROLLBAR);
  vert->mod = KMOD_NONE;
  vert->dst = pdest;

  baseclass_redraw = vert->redraw;
  vert->redraw = redraw_vert;

  return vert;
}

/**********************************************************************//**
  Draw vertical scrollbar.
**************************************************************************/
int draw_vert(struct widget *pVert, Sint16 x, Sint16 y)
{
  pVert->size.x = x;
  pVert->size.y = y;
  pVert->gfx = crop_rect_from_surface(pVert->dst->surface, &pVert->size);

  return redraw_vert(pVert);
}

/* =================================================== */
/* ===================== HSCROOLBAR ================== */
/* =================================================== */

/**********************************************************************//**
  Create background image for hscrollbars
  then return pointer to this image.

  Graphic is taken from pHoriz_theme surface and blit to new created image.

  height depend of 'Width' parameter.

  Type of image depend of "state" parameter.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled
**************************************************************************/
static SDL_Surface *create_horizontal_surface(SDL_Surface *pHoriz_theme,
                                              Uint8 state, Uint16 Width)
{
  SDL_Surface *pHorSurf = NULL;
  SDL_Rect src, des;
  Uint16 i;
  Uint16 start_x;
  Uint16 tile_count_midd;
  Uint8 tile_len_end;
  Uint8 tile_len_midd;

  tile_len_end = pHoriz_theme->w / 16;

  start_x = 0 + state * (pHoriz_theme->w / 4);

  tile_len_midd = pHoriz_theme->w / 4 - tile_len_end * 2;

  tile_count_midd = (Width - tile_len_end * 2) / tile_len_midd;

  /* correction */
  if (tile_len_midd * tile_count_midd + tile_len_end * 2 < Width) {
    tile_count_midd++;
  }

  if (!tile_count_midd) {
    pHorSurf = create_surf(tile_len_end * 2, pHoriz_theme->h, SDL_SWSURFACE);
  } else {
    pHorSurf = create_surf(Width, pHoriz_theme->h, SDL_SWSURFACE);
  }

  src.y = 0;
  src.x = start_x;
  src.h = pHoriz_theme->h;
  src.w = tile_len_end;
  alphablit(pHoriz_theme, &src, pHorSurf, NULL, 255);

  src.x = start_x + tile_len_end;
  src.w = tile_len_midd;

  des.y = 0;

  for (i = 0; i < tile_count_midd; i++) {
    des.x = tile_len_end + i * tile_len_midd;
    alphablit(pHoriz_theme, &src, pHorSurf, &des, 255);
  }

  src.x = start_x + tile_len_end + tile_len_midd;
  src.w = tile_len_end;
  des.x = pHorSurf->w - tile_len_end;
  alphablit(pHoriz_theme, &src, pHorSurf, &des, 255);

  return pHorSurf;
}

/**********************************************************************//**
  Blit horizontal scrollbar gfx to surface its on.
**************************************************************************/
static int redraw_horiz(struct widget *pHoriz)
{
  int ret;
  SDL_Rect dest = pHoriz->size;
  SDL_Surface *pHoriz_Surf;

  ret = (*baseclass_redraw)(pHoriz);
  if (ret != 0) {
    return ret;
  }
  
  pHoriz_Surf = create_horizontal_surface(pHoriz->theme,
                                          get_wstate(pHoriz),
                                          pHoriz->size.w);
  ret = blit_entire_src(pHoriz_Surf, pHoriz->dst->surface, dest.x, dest.y);

  FREESURFACE(pHoriz_Surf);

  return ret;
}

/**********************************************************************//**
  Create ( malloc ) horizontal scroll_bar Widget structure.

  Theme graphic is taken from horiz_theme surface;

  This function determinate future size of horizontal scroll_bar
  ( width = 'vert_theme->w', height = 'height' ) and
  save this in: pwidget->size rectangle ( SDL_Rect )

  Return pointer to created Widget.
**************************************************************************/
struct widget *create_horizontal(SDL_Surface *horiz_theme,
                                 struct gui_layer *pdest,
                                 Uint16 width, Uint32 flags)
{
  struct widget *hor = widget_new();

  hor->theme = horiz_theme;
  hor->size.w = width;
  hor->size.h = horiz_theme->h;
  set_wflag(hor, WF_FREE_STRING | flags);
  set_wstate(hor, FC_WS_DISABLED);
  set_wtype(hor, WT_HSCROLLBAR);
  hor->mod = KMOD_NONE;
  hor->dst = pdest;

  baseclass_redraw = hor->redraw;
  hor->redraw = redraw_horiz;

  return hor;
}

/**********************************************************************//**
  Draw horizontal scrollbar.
**************************************************************************/
int draw_horiz(struct widget *pHoriz, Sint16 x, Sint16 y)
{
  pHoriz->size.x = x;
  pHoriz->size.y = y;
  pHoriz->gfx = crop_rect_from_surface(pHoriz->dst->surface, &pHoriz->size);

  return redraw_horiz(pHoriz);
}

/* =================================================== */
/* =====================            ================== */
/* =================================================== */

/**********************************************************************//**
  Get step of the scrollbar.
**************************************************************************/
static int get_step(struct scroll_bar *scroll)
{
  float step = scroll->max - scroll->min;

  step *= (float) (1.0 - (float) (scroll->active * scroll->step) /
                   (float)scroll->count);
  step /= (float)(scroll->count - scroll->active * scroll->step);
  step *= (float)scroll->step;
  step++;

  return (int)step;
}

/**********************************************************************//**
  Get current active position of the scrollbar.
**************************************************************************/
static int get_position(struct advanced_dialog *dlg)
{
  struct widget *buf = dlg->active_widget_list;
  int count = dlg->scroll->active * dlg->scroll->step - 1;
  int step = get_step(dlg->scroll);

  /* find last seen widget */
  while (count) {
    if (buf == dlg->begin_active_widget_list) {
      break;
    }
    count--;
    buf = buf->prev;
  }

  count = 0;
  if (buf != dlg->begin_active_widget_list) {
    do {
      count++;
      buf = buf->prev;
    } while (buf != dlg->begin_active_widget_list);
  }

  if (dlg->scroll->pscroll_bar) {
    return dlg->scroll->max - dlg->scroll->pscroll_bar->size.h -
      count * (float)step / dlg->scroll->step;
  } else {
    return dlg->scroll->max - count * (float)step / dlg->scroll->step;
  }
}

/**************************************************************************
                        Vertical scroll_bar
**************************************************************************/

static struct widget *up_scroll_widget_list(struct scroll_bar *vscroll,
                                            struct widget *begin_active_widget_list,
                                            struct widget *begin_widget_list,
                                            struct widget *end_widget_list);

static struct widget *down_scroll_widget_list(struct scroll_bar *vscroll,
                                              struct widget *begin_active_widget_list,
                                              struct widget *begin_widget_list,
                                              struct widget *end_widget_list);

static struct widget *vertic_scroll_widget_list(struct scroll_bar *vscroll,
                                                struct widget *begin_active_widget_list,
                                                struct widget *begin_widget_list,
                                                struct widget *end_widget_list);

/**********************************************************************//**
  User interacted with up button of advanced dialog.
**************************************************************************/
static int std_up_advanced_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct advanced_dialog *dlg = pwidget->private_data.adv_dlg;
    struct widget *begin = up_scroll_widget_list(
                          dlg->scroll,
                          dlg->active_widget_list,
                          dlg->begin_active_widget_list,
                          dlg->end_active_widget_list);

    if (begin) {
      dlg->active_widget_list = begin;
    }

    unselect_widget_action();
    selected_widget = pwidget;
    set_wstate(pwidget, FC_WS_SELECTED);
    widget_redraw(pwidget);
    widget_flush(pwidget);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with down button of advanced dialog.
**************************************************************************/
static int std_down_advanced_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct advanced_dialog *dlg = pwidget->private_data.adv_dlg;
    struct widget *begin = down_scroll_widget_list(
                              dlg->scroll,
                              dlg->active_widget_list,
                              dlg->begin_active_widget_list,
                              dlg->end_active_widget_list);

    if (begin) {
      dlg->active_widget_list = begin;
    }

    unselect_widget_action();
    selected_widget = pwidget;
    set_wstate(pwidget, FC_WS_SELECTED);
    widget_redraw(pwidget);
    widget_flush(pwidget);
  }

  return -1;
}

/**********************************************************************//**
  FIXME : fix main funct : vertic_scroll_widget_list(...)
**************************************************************************/
static int std_vscroll_advanced_dlg_callback(struct widget *pscroll_bar)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct advanced_dialog *dlg = pscroll_bar->private_data.adv_dlg;
    struct widget *begin = vertic_scroll_widget_list(
                              dlg->scroll,
                              dlg->active_widget_list,
                              dlg->begin_active_widget_list,
                              dlg->end_active_widget_list);

    if (begin) {
      dlg->active_widget_list = begin;
    }

    unselect_widget_action();
    set_wstate(pscroll_bar, FC_WS_SELECTED);
    selected_widget = pscroll_bar;
    redraw_vert(pscroll_bar);
    widget_flush(pscroll_bar);
  }

  return -1;
}

/**********************************************************************//**
  Create a new vertical scrollbar to active widgets list.
**************************************************************************/
Uint32 create_vertical_scrollbar(struct advanced_dialog *dlg,
                                 Uint8 step, Uint8 active,
                                 bool create_scrollbar, bool create_buttons)
{
  Uint16 count = 0;
  struct widget *buf = NULL, *pwindow = NULL;

  fc_assert_ret_val(dlg != NULL, 0);

  pwindow = dlg->end_widget_list;

  if (!dlg->scroll) {
    dlg->scroll = fc_calloc(1, sizeof(struct scroll_bar));

    buf = dlg->end_active_widget_list;
    while (buf && (buf != dlg->begin_active_widget_list->prev)) {
      buf = buf->prev;
      count++;
    }

    dlg->scroll->count = count;
  }

  dlg->scroll->active = active;
  dlg->scroll->step = step;

  if (create_buttons) {
    /* create up button */
    buf = create_themeicon_button(current_theme->up_icon, pwindow->dst,
                                  NULL, WF_RESTORE_BACKGROUND);

    buf->id = ID_BUTTON;
    buf->private_data.adv_dlg = dlg;
    buf->action = std_up_advanced_dlg_callback;
    set_wstate(buf, FC_WS_NORMAL);

    dlg->scroll->up_left_button = buf;
    widget_add_as_prev(buf, dlg->begin_widget_list);
    dlg->begin_widget_list = buf;

    count = buf->size.w;

    /* create down button */
    buf = create_themeicon_button(current_theme->down_icon, pwindow->dst,
                                  NULL, WF_RESTORE_BACKGROUND);

    buf->id = ID_BUTTON;
    buf->private_data.adv_dlg = dlg;
    buf->action = std_down_advanced_dlg_callback;
    set_wstate(buf, FC_WS_NORMAL);

    dlg->scroll->down_right_button = buf;
    widget_add_as_prev(buf, dlg->begin_widget_list);
    dlg->begin_widget_list = buf;
  }

  if (create_scrollbar) {
    /* create vscrollbar */
    buf = create_vertical(current_theme->vertic, pwindow->dst,
                          adj_size(10), WF_RESTORE_BACKGROUND);

    buf->id = ID_SCROLLBAR;
    buf->private_data.adv_dlg = dlg;
    buf->action = std_vscroll_advanced_dlg_callback;
    set_wstate(buf, FC_WS_NORMAL);

    dlg->scroll->pscroll_bar = buf;
    widget_add_as_prev(buf, dlg->begin_widget_list);
    dlg->begin_widget_list = buf;

    if (!count) {
      count = buf->size.w;
    }
  }

  return count;
}

/**********************************************************************//**
  Setup area for the vertical scrollbar.
**************************************************************************/
void setup_vertical_scrollbar_area(struct scroll_bar *scroll,
                                   Sint16 start_x, Sint16 start_y,
                                   Uint16 height, bool swap_start_x)
{
  bool buttons_exist;

  fc_assert_ret(scroll != NULL);

  buttons_exist = (scroll->down_right_button && scroll->up_left_button);

  if (buttons_exist) {
    /* up */
    scroll->up_left_button->size.y = start_y;
    if (swap_start_x) {
      scroll->up_left_button->size.x = start_x -
        scroll->up_left_button->size.w;
    } else {
      scroll->up_left_button->size.x = start_x;
    }
    scroll->min = start_y + scroll->up_left_button->size.h;
    /* -------------------------- */
    /* down */
    scroll->down_right_button->size.y = start_y + height -
      scroll->down_right_button->size.h;
    if (swap_start_x) {
      scroll->down_right_button->size.x = start_x -
        scroll->down_right_button->size.w;
    } else {
      scroll->down_right_button->size.x = start_x;
    }
    scroll->max = scroll->down_right_button->size.y;
  }
  /* --------------- */
  /* scrollbar */
  if (scroll->pscroll_bar) {
    if (swap_start_x) {
      scroll->pscroll_bar->size.x = start_x - scroll->pscroll_bar->size.w + 2;
    } else {
      scroll->pscroll_bar->size.x = start_x;
    }

    if (buttons_exist) {
      scroll->pscroll_bar->size.y = start_y +
        scroll->up_left_button->size.h;
      if (scroll->count > scroll->active * scroll->step) {
        scroll->pscroll_bar->size.h = scrollbar_size(scroll);
      } else {
        scroll->pscroll_bar->size.h = scroll->max - scroll->min;
      }
    } else {
      scroll->pscroll_bar->size.y = start_y;
      scroll->pscroll_bar->size.h = height;
      scroll->min = start_y;
      scroll->max = start_y + height;
    }
  }
}

/* =================================================== */
/* ============ Vertical Scroll Group List =========== */
/* =================================================== */

/**********************************************************************//**
  scroll pointers on list.
  dir == directions: up == -1, down == 1.
**************************************************************************/
static struct widget *vertical_scroll_widget_list(struct widget *pActiveWidgetLIST,
                                                  struct widget *begin_widget_list,
                                                  struct widget *end_widget_list,
                                                  int active, int step, int dir)
{
  struct widget *begin = pActiveWidgetLIST;
  struct widget *buf = pActiveWidgetLIST;
  struct widget *tmp = NULL;
  int count = active; /* row */
  int count_step = step; /* col */

  if (dir < 0) {
    /* up */
    bool real = TRUE;

    if (buf != end_widget_list) {
      /*
       move pointers to positions and unhide scrolled widgets
       B = buf - new top
       T = tmp - current top == pActiveWidgetLIST
       [B] [ ] [ ]
       -----------
       [T] [ ] [ ]
       [ ] [ ] [ ]
       -----------
       [ ] [ ] [ ]
    */
      tmp = buf; /* now buf == pActiveWidgetLIST == current Top */
      while (count_step > 0) {
        buf = buf->next;
        clear_wflag(buf, WF_HIDDEN);
        count_step--;
      }
      count_step = step;

      /* setup new ActiveWidget pointer */
      begin = buf;

      /*
       scroll pointers up
       B = buf
       T = tmp
       [B0] [B1] [B2]
       -----------
       [T0] [T1] [T2]   => B position = T position
       [T3] [T4] [T5]
       -----------
       [  ] [  ] [  ]

       start from B0 and go down list
       B0 = T0, B1 = T1, B2 = T2
       T0 = T3, T1 = T4, T2 = T5
       etc...
    */

      /* buf == begin == new top widget */

      while (count > 0) {
        if (real) {
          buf->size.x = tmp->size.x;
          buf->size.y = tmp->size.y;
          buf->gfx = tmp->gfx;

          if ((buf->size.w != tmp->size.w) || (buf->size.h != tmp->size.h)) {
            widget_undraw(tmp);
            widget_mark_dirty(tmp);
            if (get_wflags(buf) & WF_RESTORE_BACKGROUND) {
              refresh_widget_background(buf);
            }
          }

          tmp->gfx = NULL;

          if (count == 1) {
            set_wflag(tmp, WF_HIDDEN);
          }
          if (tmp == begin_widget_list) {
            real = FALSE;
          }
          tmp = tmp->prev;
        } else {
          /*
            unsymmetric list support.
            This is big problem because we can't take position from unexisting
            list memeber. We must put here some hypothetical positions

            [B0] [B1] [B2]
            --------------
            [T0] [T1]

          */
          if (active > 1) {
            /* this works well if active > 1 but is buggy when active == 1 */
            buf->size.y += buf->size.h;
          } else {
            /* this works well if active == 1 but may be broken if "next"
               element has other "y" position */
            buf->size.y = buf->next->size.y;
          }
          buf->gfx = NULL;
        }

        buf = buf->prev;
        count_step--;
        if (!count_step) {
          count_step = step;
          count--;
        }
      }
    }
  } else {
    /* down */
    count = active * step; /* row * col */

    /*
       find end
       B = buf
       A - start (buf == pActiveWidgetLIST)
       [ ] [ ] [ ]
       -----------
       [A] [ ] [ ]
       [ ] [ ] [ ]
       -----------
       [B] [ ] [ ]
    */
    while (count && buf != begin_widget_list->prev) {
      buf = buf->prev;
      count--;
    }

    if (!count && buf != begin_widget_list->prev) {
      /*
       move pointers to positions and unhide scrolled widgets
       B = buf
       T = tmp
       A - start (pActiveWidgetLIST)
       [ ] [ ] [ ]
       -----------
       [A] [ ] [ ]
       [ ] [ ] [T]
       -----------
       [ ] [ ] [B]
    */
      tmp = buf->next;
      count_step = step - 1;
      while (count_step && buf != begin_widget_list) {
        clear_wflag(buf, WF_HIDDEN);
        buf = buf->prev;
        count_step--;
      }
      clear_wflag(buf, WF_HIDDEN);

      /*
        Unsymmetric list support.
        correct tmp and undraw empty fields
        B = buf
        T = tmp
        A - start (pActiveWidgetLIST)
        [ ] [ ] [ ]
        -----------
        [A] [ ] [ ]
        [ ] [T] [U]  <- undraw U
        -----------
        [ ] [B]
      */
      count = count_step;
      while (count) {
        /* hack - clear area under unexisting list members */
        widget_undraw(tmp);
        widget_mark_dirty(tmp);
        FREESURFACE(tmp->gfx);
        if (active == 1) {
          set_wflag(tmp, WF_HIDDEN);
        }
        tmp = tmp->next;
        count--;
      }

      /* reset counters */
      count = active;
      if (count_step) {
        count_step = step - count_step;
      } else {
        count_step = step;
      }

      /*
        scroll pointers down
        B = buf
        T = tmp
        [  ] [  ] [  ]
        -----------
        [  ] [  ] [  ]
        [T2] [T1] [T0]   => B position = T position
        -----------
        [B2] [B1] [B0]
      */
      while (count) {
        buf->size.x = tmp->size.x;
        buf->size.y = tmp->size.y;
        buf->gfx = tmp->gfx;

        if ((buf->size.w != tmp->size.w) || (buf->size.h != tmp->size.h)) {
          widget_undraw(tmp);
          widget_mark_dirty(tmp);
          if (get_wflags(buf) & WF_RESTORE_BACKGROUND) {
            refresh_widget_background(buf);
          }
        }

        tmp->gfx = NULL;

        if (count == 1) {
          set_wflag(tmp, WF_HIDDEN);
        }

        tmp = tmp->next;
        buf = buf->next;
        count_step--;
        if (!count_step) {
          count_step = step;
          count--;
        }
      }
      /* setup new ActiveWidget pointer */
      begin = buf->prev;
    }
  }

  return begin;
}

/**********************************************************************//**
  Callback for the scroll-down event loop.
**************************************************************************/
static void inside_scroll_down_loop(void *pData)
{
  struct UP_DOWN *pDown = (struct UP_DOWN *)pData;

  if (pDown->end != pDown->begin_widget_list) {
    if (pDown->vscroll->pscroll_bar
        && pDown->vscroll->pscroll_bar->size.y <=
        pDown->vscroll->max - pDown->vscroll->pscroll_bar->size.h) {

      /* draw bcgd */
      widget_undraw(pDown->vscroll->pscroll_bar);
      widget_mark_dirty(pDown->vscroll->pscroll_bar);

      if (pDown->vscroll->pscroll_bar->size.y + pDown->step >
          pDown->vscroll->max - pDown->vscroll->pscroll_bar->size.h) {
        pDown->vscroll->pscroll_bar->size.y =
          pDown->vscroll->max - pDown->vscroll->pscroll_bar->size.h;
      } else {
        pDown->vscroll->pscroll_bar->size.y += pDown->step;
      }
    }

    pDown->begin = vertical_scroll_widget_list(pDown->begin,
                  pDown->begin_widget_list, pDown->end_widget_list,
                  pDown->vscroll->active, pDown->vscroll->step, 1);

    pDown->end = pDown->end->prev;

    redraw_group(pDown->begin_widget_list, pDown->end_widget_list, TRUE);

    if (pDown->vscroll->pscroll_bar) {
      /* redraw scrollbar */
      if (get_wflags(pDown->vscroll->pscroll_bar) & WF_RESTORE_BACKGROUND) {
        refresh_widget_background(pDown->vscroll->pscroll_bar);
      }
      redraw_vert(pDown->vscroll->pscroll_bar);

      widget_mark_dirty(pDown->vscroll->pscroll_bar);
    }

    flush_dirty();
  }
}

/**********************************************************************//**
  Callback for the scroll-up event loop.
**************************************************************************/
static void inside_scroll_up_loop(void *pData)
{
  struct UP_DOWN *pUp = (struct UP_DOWN *)pData;

  if (pUp && pUp->begin != pUp->end_widget_list) {

    if (pUp->vscroll->pscroll_bar
        && (pUp->vscroll->pscroll_bar->size.y >= pUp->vscroll->min)) {

      /* draw bcgd */
      widget_undraw(pUp->vscroll->pscroll_bar);
      widget_mark_dirty(pUp->vscroll->pscroll_bar);

      if (((pUp->vscroll->pscroll_bar->size.y - pUp->step) < pUp->vscroll->min)) {
        pUp->vscroll->pscroll_bar->size.y = pUp->vscroll->min;
      } else {
        pUp->vscroll->pscroll_bar->size.y -= pUp->step;
      }
    }

    pUp->begin = vertical_scroll_widget_list(pUp->begin,
                        pUp->begin_widget_list, pUp->end_widget_list,
                        pUp->vscroll->active, pUp->vscroll->step, -1);

    redraw_group(pUp->begin_widget_list, pUp->end_widget_list, TRUE);

    if (pUp->vscroll->pscroll_bar) {
      /* redraw scroolbar */
      if (get_wflags(pUp->vscroll->pscroll_bar) & WF_RESTORE_BACKGROUND) {
        refresh_widget_background(pUp->vscroll->pscroll_bar);
      }
      redraw_vert(pUp->vscroll->pscroll_bar);
      widget_mark_dirty(pUp->vscroll->pscroll_bar);
    }

    flush_dirty();
  }
}

/**********************************************************************//**
  Handle mouse motion events of the vertical scrollbar event loop.
**************************************************************************/
static Uint16 scroll_mouse_motion_handler(SDL_MouseMotionEvent *motion_event,
                                          void *pData)
{
  struct UP_DOWN *motion = (struct UP_DOWN *)pData;
  int yrel;
  int y;
  int normalized_y;
  int net_slider_area;
  int net_count;
  float scroll_step;

  yrel = motion_event->y - motion->prev_y;
  motion->prev_x = motion_event->x;
  motion->prev_y = motion_event->y;

  y = motion_event->y - motion->vscroll->pscroll_bar->dst->dest_rect.y;

  normalized_y = (y - motion->offset);

  net_slider_area = (motion->vscroll->max - motion->vscroll->min - motion->vscroll->pscroll_bar->size.h);
  net_count = round((float)motion->vscroll->count / motion->vscroll->step) - motion->vscroll->active + 1;
  scroll_step = (float)net_slider_area / net_count;
  
  if ((yrel != 0)
      && ((normalized_y >= motion->vscroll->min)
          || ((normalized_y < motion->vscroll->min)
              && (motion->vscroll->pscroll_bar->size.y > motion->vscroll->min)))
      && ((normalized_y <= motion->vscroll->max - motion->vscroll->pscroll_bar->size.h)
          || ((normalized_y > motion->vscroll->max)
              && (motion->vscroll->pscroll_bar->size.y < (motion->vscroll->max - motion->vscroll->pscroll_bar->size.h)))) ) {
    int count;

    /* draw bcgd */
    widget_undraw(motion->vscroll->pscroll_bar);
    widget_mark_dirty(motion->vscroll->pscroll_bar);

    if ((motion->vscroll->pscroll_bar->size.y + yrel) >
        (motion->vscroll->max - motion->vscroll->pscroll_bar->size.h)) {

      motion->vscroll->pscroll_bar->size.y =
        (motion->vscroll->max - motion->vscroll->pscroll_bar->size.h);

    } else if ((motion->vscroll->pscroll_bar->size.y + yrel) < motion->vscroll->min) {
      motion->vscroll->pscroll_bar->size.y = motion->vscroll->min;
    } else {
      motion->vscroll->pscroll_bar->size.y += yrel;
    }

    count = round((motion->vscroll->pscroll_bar->size.y - motion->old_y) / scroll_step);

    if (count != 0) {
      int i = count;

      while (i != 0) {
        motion->begin = vertical_scroll_widget_list(motion->begin,
                                                    motion->begin_widget_list,
                                                    motion->end_widget_list,
                                                    motion->vscroll->active,
                                                    motion->vscroll->step, i);
        if (i > 0) {
          i--;
        } else {
          i++;
        }
      } /* while (i != 0) */

      motion->old_y = motion->vscroll->min +
        ((round((motion->old_y - motion->vscroll->min) / scroll_step) + count) * scroll_step);

      redraw_group(motion->begin_widget_list, motion->end_widget_list, TRUE);
    }

    /* redraw slider */
    if (get_wflags(motion->vscroll->pscroll_bar) & WF_RESTORE_BACKGROUND) {
      refresh_widget_background(motion->vscroll->pscroll_bar);
    }
    redraw_vert(motion->vscroll->pscroll_bar);
    widget_mark_dirty(motion->vscroll->pscroll_bar);

    flush_dirty();
  }

  return ID_ERROR;
}

/**********************************************************************//**
  Callback for scrollbar event loops' mouse up events.
**************************************************************************/
static Uint16 scroll_mouse_button_up(SDL_MouseButtonEvent *button_event,
                                     void *pData)
{
  return (Uint16)ID_SCROLLBAR;
}

/**********************************************************************//**
  Scroll widgets down.
**************************************************************************/
static struct widget *down_scroll_widget_list(struct scroll_bar *vscroll,
                                              struct widget *begin_active_widget_list,
                                              struct widget *begin_widget_list,
                                              struct widget *end_widget_list)
{
  struct UP_DOWN pDown;
  struct widget *begin = begin_active_widget_list;
  int step = vscroll->active * vscroll->step - 1;

  while (step--) {
    begin = begin->prev;
  }

  pDown.step = get_step(vscroll);
  pDown.begin = begin_active_widget_list;
  pDown.end = begin;
  pDown.begin_widget_list = begin_widget_list;
  pDown.end_widget_list = end_widget_list;
  pDown.vscroll = vscroll;

  gui_event_loop((void *)&pDown, inside_scroll_down_loop, NULL, NULL, NULL,
                 NULL, NULL, NULL, NULL, scroll_mouse_button_up, NULL);

  return pDown.begin;
}

/**********************************************************************//**
  Scroll widgets up.
**************************************************************************/
static struct widget *up_scroll_widget_list(struct scroll_bar *vscroll,
                                            struct widget *begin_active_widget_list,
                                            struct widget *begin_widget_list,
                                            struct widget *end_widget_list)
{
  struct UP_DOWN pUp;

  pUp.step = get_step(vscroll);
  pUp.begin = begin_active_widget_list;
  pUp.begin_widget_list = begin_widget_list;
  pUp.end_widget_list = end_widget_list;
  pUp.vscroll = vscroll;

  gui_event_loop((void *)&pUp, inside_scroll_up_loop, NULL, NULL, NULL,
                 NULL, NULL, NULL, NULL, scroll_mouse_button_up, NULL);

  return pUp.begin;
}

/**********************************************************************//**
  Scroll vertical widget list with the mouse movement.
**************************************************************************/
static struct widget *vertic_scroll_widget_list(struct scroll_bar *vscroll,
                                                struct widget *begin_active_widget_list,
                                                struct widget *begin_widget_list,
                                                struct widget *end_widget_list)
{
  struct UP_DOWN motion;

  motion.step = get_step(vscroll);
  motion.begin = begin_active_widget_list;
  motion.begin_widget_list = begin_widget_list;
  motion.end_widget_list = end_widget_list;
  motion.vscroll = vscroll;
  motion.old_y = vscroll->pscroll_bar->size.y;
  SDL_GetMouseState(&motion.prev_x, &motion.prev_y);
  motion.offset = motion.prev_y - vscroll->pscroll_bar->dst->dest_rect.y - vscroll->pscroll_bar->size.y;

  MOVE_STEP_X = 0;
  MOVE_STEP_Y = 3;
  /* Filter mouse motion events */
  SDL_SetEventFilter(FilterMouseMotionEvents, NULL);
  gui_event_loop((void *)&motion, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                 scroll_mouse_button_up, scroll_mouse_motion_handler);
  /* Turn off Filter mouse motion events */
  SDL_SetEventFilter(NULL, NULL);
  MOVE_STEP_X = DEFAULT_MOVE_STEP;
  MOVE_STEP_Y = DEFAULT_MOVE_STEP;

  return motion.begin;
}

/* ==================================================================== */

/**********************************************************************//**
  Add new widget to scrolled list and set draw position of all changed widgets.
  dir :
    TRUE - upper add => add_dock->next = new_widget.
    FALSE - down add => add_dock->prev = new_widget.
  start_x, start_y - positions of first seen widget (active_widget_list).
  dlg->scroll ( scrollbar ) must exist.
  It isn't full secure to multi widget list.
**************************************************************************/
bool add_widget_to_vertical_scroll_widget_list(struct advanced_dialog *dlg,
                                               struct widget *new_widget,
                                               struct widget *add_dock,
                                               bool dir,
                                               Sint16 start_x, Sint16 start_y)
{
  struct widget *buf = NULL;
  struct widget *end = NULL, *pOld_End = NULL;
  int count = 0;
  bool last = FALSE, seen = TRUE;

  fc_assert_ret_val(new_widget != NULL, FALSE);
  fc_assert_ret_val(dlg != NULL, FALSE);
  fc_assert_ret_val(dlg->scroll != NULL, FALSE);

  if (!add_dock) {
    add_dock = dlg->begin_widget_list; /* last item */
  }

  dlg->scroll->count++;

  if (dlg->scroll->count > (dlg->scroll->active * dlg->scroll->step)) {
    /* -> scrollbar needed */

    if (dlg->active_widget_list) {
      /* -> scrollbar is already visible */
      int i = 0;

      /* find last active widget */
      pOld_End = add_dock;
      while (pOld_End != dlg->active_widget_list) {
        pOld_End = pOld_End->next;
        i++;
        if (pOld_End == dlg->end_active_widget_list) {
          /* implies (pOld_End == dlg->active_widget_list)? */
          seen = FALSE;
          break;
        }
      }

      if (seen) {
        count = (dlg->scroll->active * dlg->scroll->step) - 1;
        if (i > count) {
          seen = FALSE;
        } else {
          while (count > 0) {
            pOld_End = pOld_End->prev;
            count--;
          }
          if (pOld_End == add_dock) {
            last = TRUE;
          }
        }
      }

    } else {
      last = TRUE;
      dlg->active_widget_list = dlg->end_active_widget_list;
      show_scrollbar(dlg->scroll);
    }
  }

  count = 0;

  /* add Pointer to list */
  if (dir) {
    /* upper add */
    widget_add_next(new_widget, add_dock);

    if (add_dock == dlg->end_widget_list) {
      dlg->end_widget_list = new_widget;
    }
    if (add_dock == dlg->end_active_widget_list) {
      dlg->end_active_widget_list = new_widget;
    }
    if (add_dock == dlg->active_widget_list) {
      dlg->active_widget_list = new_widget;
    }
  } else {
    /* down add */
    widget_add_as_prev(new_widget, add_dock);

    if (add_dock == dlg->begin_widget_list) {
      dlg->begin_widget_list = new_widget;
    }

    if (add_dock == dlg->begin_active_widget_list) {
      dlg->begin_active_widget_list = new_widget;
    }
  }

  /* setup draw positions */
  if (seen) {
    if (!dlg->begin_active_widget_list) {
      /* first element ( active list empty ) */
      fc_assert_msg(FALSE == dir, "Forbided List Operation");
      new_widget->size.x = start_x;
      new_widget->size.y = start_y;
      dlg->begin_active_widget_list = new_widget;
      dlg->end_active_widget_list = new_widget;
      if (!dlg->begin_widget_list) {
        dlg->begin_widget_list = new_widget;
        dlg->end_widget_list = new_widget;
      }
    } else { /* there are some elements on local active list */
      if (last) {
        /* We add to last seen position */
        if (dir) {
          /* only swap add_dock with new_widget on last seen positions */
          new_widget->size.x = add_dock->size.x;
          new_widget->size.y = add_dock->size.y;
          new_widget->gfx = add_dock->gfx;
          add_dock->gfx = NULL;
          set_wflag(add_dock, WF_HIDDEN);
        } else {
          /* reposition all widgets */
          buf = new_widget;
          do {
            buf->size.x = buf->next->size.x;
            buf->size.y = buf->next->size.y;
            buf->gfx = buf->next->gfx;
            buf->next->gfx = NULL;
            buf = buf->next;
          } while (buf != dlg->active_widget_list);
          buf->gfx = NULL;
          set_wflag(buf, WF_HIDDEN);
          dlg->active_widget_list = dlg->active_widget_list->prev;
        }
      } else { /* !last */
        buf = new_widget;
        /* find last seen widget */
        if (dlg->active_widget_list) {
          end = dlg->active_widget_list;
          count = dlg->scroll->active * dlg->scroll->step - 1;
          while (count && end != dlg->begin_active_widget_list) {
            end = end->prev;
            count--;
          }
        }
        while (buf) {
          if (buf == dlg->begin_active_widget_list) {
            struct widget *tmp = buf;

            count = dlg->scroll->step;
            while (count) {
              tmp = tmp->next;
              count--;
            }
            buf->size.x = tmp->size.x;
            buf->size.y = tmp->size.y + tmp->size.h;
            /* break when last active widget or last seen widget */
            break;
          } else {
            buf->size.x = buf->prev->size.x;
            buf->size.y = buf->prev->size.y;
            buf->gfx = buf->prev->gfx;
            buf->prev->gfx = NULL;
            if (buf == end) {
              break;
            }
          }
          buf = buf->prev;
        }
        if (pOld_End && buf->prev == pOld_End) {
          set_wflag(pOld_End, WF_HIDDEN);
        }
      } /* !last */
    } /* dlg->begin_active_widget_list */
  } else { /* !seen */
    set_wflag(new_widget, WF_HIDDEN);
  }

  if (dlg->active_widget_list && dlg->scroll->pscroll_bar) {
    widget_undraw(dlg->scroll->pscroll_bar);
    widget_mark_dirty(dlg->scroll->pscroll_bar);

    dlg->scroll->pscroll_bar->size.h = scrollbar_size(dlg->scroll);
    if (last) {
      dlg->scroll->pscroll_bar->size.y = get_position(dlg);
    }
    if (get_wflags(dlg->scroll->pscroll_bar) & WF_RESTORE_BACKGROUND) {
      refresh_widget_background(dlg->scroll->pscroll_bar);
    }
    if (!seen) {
      redraw_vert(dlg->scroll->pscroll_bar);
    }
  }

  return last;
}

/**********************************************************************//**
  Delete widget from scrolled list and set draw position of all changed
  widgets.
  Don't free dlg and dlg->scroll (if exist)
  It is full secure for multi widget list case.
**************************************************************************/
bool del_widget_from_vertical_scroll_widget_list(struct advanced_dialog *dlg,
                                                 struct widget *pwidget)
{
  int count = 0;
  struct widget *buf = pwidget;

  fc_assert_ret_val(pwidget != NULL, FALSE);
  fc_assert_ret_val(dlg != NULL, FALSE);

  /* if begin == end -> size = 1 */
  if (dlg->begin_active_widget_list == dlg->end_active_widget_list) {

    if (dlg->scroll) {
      dlg->scroll->count = 0;
    }

    if (dlg->begin_active_widget_list == dlg->begin_widget_list) {
      dlg->begin_widget_list = dlg->begin_widget_list->next;
    }

    if (dlg->end_active_widget_list == dlg->end_widget_list) {
      dlg->end_widget_list = dlg->end_widget_list->prev;
    }

    dlg->begin_active_widget_list = NULL;
    dlg->active_widget_list = NULL;
    dlg->end_active_widget_list = NULL;

    widget_undraw(pwidget);
    widget_mark_dirty(pwidget);
    del_widget_from_gui_list(pwidget);
    return FALSE;
  }

  if (dlg->scroll && dlg->active_widget_list) {
    /* scrollbar exist and active, start mod. from last seen label */
    struct widget *last;
    bool widget_found = FALSE;

    /* this is always true because no-scrolbar case (active*step < count)
       will be supported in other part of code (see "else" part) */
    count = dlg->scroll->active * dlg->scroll->step;

    /* find last */
    buf = dlg->active_widget_list;
    while (count > 0) {
      buf = buf->prev;
      count--;
    }
    if (!buf) {
      last = dlg->begin_active_widget_list;
    } else {
      last = buf->next;
    }

    if (last == dlg->begin_active_widget_list) {
      if (dlg->scroll->step == 1) {
        dlg->active_widget_list = dlg->active_widget_list->next;
        clear_wflag(dlg->active_widget_list, WF_HIDDEN);

        /* look for the widget in the non-visible part */
        buf = dlg->end_active_widget_list;
        while (buf != dlg->active_widget_list) {
          if (buf == pwidget) {
            widget_found = TRUE;
            buf = dlg->active_widget_list;
            break;
          }
          buf = buf->prev;
        }

        /* if we haven't found it yet, look in the visible part and update the
         * positions of the other widgets */
        if (!widget_found) {
          while (buf != pwidget) {
            buf->gfx = buf->prev->gfx;
            buf->prev->gfx = NULL;
            buf->size.x = buf->prev->size.x;
            buf->size.y = buf->prev->size.y;
            buf = buf->prev;
          }
        }
      } else {
        buf = last;
        /* undraw last widget */
        widget_undraw(buf);
        widget_mark_dirty(buf);
        FREESURFACE(buf->gfx);
        goto STD;
      }
    } else {
      clear_wflag(buf, WF_HIDDEN);
STD:  while (buf != pwidget) {
        buf->gfx = buf->next->gfx;
        buf->next->gfx = NULL;
        buf->size.x = buf->next->size.x;
        buf->size.y = buf->next->size.y;
        buf = buf->next;
      }
    }

    if ((dlg->scroll->count - 1) <= (dlg->scroll->active * dlg->scroll->step)) {
      /* scrollbar not needed anymore */
      hide_scrollbar(dlg->scroll);
      dlg->active_widget_list = NULL;
    }
    dlg->scroll->count--;

    if (dlg->active_widget_list) {
      if (dlg->scroll->pscroll_bar) {
        widget_undraw(dlg->scroll->pscroll_bar);
        dlg->scroll->pscroll_bar->size.h = scrollbar_size(dlg->scroll);
        refresh_widget_background(dlg->scroll->pscroll_bar);
      }
    }

  } else { /* no scrollbar */
    buf = dlg->begin_active_widget_list;

    /* undraw last widget */
    widget_undraw(buf);
    widget_mark_dirty(buf);
    FREESURFACE(buf->gfx);

    while (buf != pwidget) {
      buf->gfx = buf->next->gfx;
      buf->next->gfx = NULL;
      buf->size.x = buf->next->size.x;
      buf->size.y = buf->next->size.y;
      buf = buf->next;
    }

    if (dlg->scroll) {
      dlg->scroll->count--;
    }
  }

  if (pwidget == dlg->begin_widget_list) {
    dlg->begin_widget_list = pwidget->next;
  }

  if (pwidget == dlg->begin_active_widget_list) {
    dlg->begin_active_widget_list = pwidget->next;
  }

  if (pwidget == dlg->end_active_widget_list) {
    if (pwidget == dlg->end_widget_list) {
      dlg->end_widget_list = pwidget->prev;
    }

    if (pwidget == dlg->active_widget_list) {
      dlg->active_widget_list = pwidget->prev;
    }

    dlg->end_active_widget_list = pwidget->prev;

  }

  if (dlg->active_widget_list && (dlg->active_widget_list == pwidget)) {
    dlg->active_widget_list = pwidget->prev;
  }

  del_widget_from_gui_list(pwidget);

  if (dlg->scroll && dlg->scroll->pscroll_bar && dlg->active_widget_list) {
    widget_undraw(dlg->scroll->pscroll_bar);
    dlg->scroll->pscroll_bar->size.y = get_position(dlg);
    refresh_widget_background(dlg->scroll->pscroll_bar);
  }

  return TRUE;
}

/**********************************************************************//**
  Set default vertical scrollbar handling for scrollbar.
**************************************************************************/
void setup_vertical_scrollbar_default_callbacks(struct scroll_bar *scroll)
{
  fc_assert_ret(scroll != NULL);

  if (scroll->up_left_button) {
    scroll->up_left_button->action = std_up_advanced_dlg_callback;
  }
  if (scroll->down_right_button) {
    scroll->down_right_button->action = std_down_advanced_dlg_callback;
  }
  if (scroll->pscroll_bar) {
    scroll->pscroll_bar->action = std_vscroll_advanced_dlg_callback;
  }
}

/**************************************************************************
                        Horizontal Scrollbar
**************************************************************************/


/**********************************************************************//**
  Create a new horizontal scrollbar to active widgets list.
**************************************************************************/
Uint32 create_horizontal_scrollbar(struct advanced_dialog *dlg,
                                   Sint16 start_x, Sint16 start_y,
                                   Uint16 width, Uint16 active,
                                   bool create_scrollbar, bool create_buttons,
                                   bool swap_start_y)
{
  Uint16 count = 0;
  struct widget *buf = NULL, *pwindow = NULL;

  fc_assert_ret_val(dlg != NULL, 0);

  pwindow = dlg->end_widget_list;

  if (!dlg->scroll) {
    dlg->scroll = fc_calloc(1, sizeof(struct scroll_bar));

    dlg->scroll->active = active;

    buf = dlg->end_active_widget_list;
    while (buf && buf != dlg->begin_active_widget_list->prev) {
      buf = buf->prev;
      count++;
    }

    dlg->scroll->count = count;
  }

  if (create_buttons) {
    /* create up button */
    buf = create_themeicon_button(current_theme->left_icon, pwindow->dst, NULL, 0);

    buf->id = ID_BUTTON;
    buf->data.ptr = (void *)dlg;
    set_wstate(buf, FC_WS_NORMAL);

    buf->size.x = start_x;
    if (swap_start_y) {
      buf->size.y = start_y - buf->size.h;
    } else {
      buf->size.y = start_y;
    }

    dlg->scroll->min = start_x + buf->size.w;
    dlg->scroll->up_left_button = buf;
    widget_add_as_prev(buf, dlg->begin_widget_list);
    dlg->begin_widget_list = buf;

    count = buf->size.h;

    /* create down button */
    buf = create_themeicon_button(current_theme->right_icon, pwindow->dst, NULL, 0);

    buf->id = ID_BUTTON;
    buf->data.ptr = (void *)dlg;
    set_wstate(buf, FC_WS_NORMAL);

    buf->size.x = start_x + width - buf->size.w;
    if (swap_start_y) {
      buf->size.y = start_y - buf->size.h;
    } else {
      buf->size.y = start_y;
    }

    dlg->scroll->max = buf->size.x;
    dlg->scroll->down_right_button = buf;
    widget_add_as_prev(buf, dlg->begin_widget_list);
    dlg->begin_widget_list = buf;
  }

  if (create_scrollbar) {
    /* create vscrollbar */
    buf = create_horizontal(current_theme->horiz, pwindow->dst,
                            width, WF_RESTORE_BACKGROUND);

    buf->id = ID_SCROLLBAR;
    buf->data.ptr = (void *)dlg;
    set_wstate(buf, FC_WS_NORMAL);

    if (swap_start_y) {
      buf->size.y = start_y - buf->size.h;
    } else {
      buf->size.y = start_y;
    }

    if (create_buttons) {
      buf->size.x = start_x + dlg->scroll->up_left_button->size.w;
      if (dlg->scroll->count > dlg->scroll->active) {
        buf->size.w = scrollbar_size(dlg->scroll);
      } else {
        buf->size.w = dlg->scroll->max - dlg->scroll->min;
      }
    } else {
      buf->size.x = start_x;
      dlg->scroll->min = start_x;
      dlg->scroll->max = start_x + width;
    }

    dlg->scroll->pscroll_bar = buf;
    widget_add_as_prev(buf, dlg->begin_widget_list);
    dlg->begin_widget_list = buf;

    if (!count) {
      count = buf->size.h;
    }
  }

  return count;
}
