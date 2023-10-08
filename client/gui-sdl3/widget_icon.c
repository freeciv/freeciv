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
#include "themespec.h"

#include "widget.h"
#include "widget_p.h"

static int (*baseclass_redraw)(struct widget *pwidget);

/* =================================================== */
/* ===================== ICON ======================== */
/* =================================================== */

/**********************************************************************//**
  Blit icon gfx to surface its on.
**************************************************************************/
static int redraw_icon(struct widget *icon)
{
  int ret;
  SDL_Rect src, area = icon->size;

  ret = (*baseclass_redraw)(icon);
  if (ret != 0) {
    return ret;
  }

  if (!icon->theme) {
    return -3;
  }

  src.x = (icon->theme->w / 4) * (Uint8) (get_wstate(icon));
  src.y = 0;
  src.w = (icon->theme->w / 4);
  src.h = icon->theme->h;

  if (icon->size.w != src.w) {
    area.x += (icon->size.w - src.w) / 2;
  }

  if (icon->size.h != src.h) {
    area.y += (icon->size.h - src.h) / 2;
  }

  return alphablit(icon->theme, &src, icon->dst->surface, &area, 255);
}

/**********************************************************************//**
  Blit icon2 gfx to surface its on.
**************************************************************************/
static int redraw_icon2(struct widget *icon)
{
  int ret;
  SDL_Rect dest;

  ret = (*baseclass_redraw)(icon);
  if (ret != 0) {
    return ret;
  }

  if (!icon) {
    return -3;
  }

  if (!icon->theme) {
    return -4;
  }

  dest.x = icon->size.x;
  dest.y = icon->size.y;
  dest.w = icon->theme->w;
  dest.h = icon->theme->h;

  switch (get_wstate(icon)) {
  case FC_WS_SELECTED:
    create_frame(icon->dst->surface,
                 dest.x + 1, dest.y + 1,
                 dest.w + adj_size(2), dest.h + adj_size(2),
                 get_theme_color(COLOR_THEME_CUSTOM_WIDGET_SELECTED_FRAME));
    break;

  case FC_WS_PRESSED:
    create_frame(icon->dst->surface,
                 dest.x + 1, dest.y + 1,
                 dest.w + adj_size(2), dest.h + adj_size(2),
                 get_theme_color(COLOR_THEME_CUSTOM_WIDGET_SELECTED_FRAME));

    create_frame(icon->dst->surface,
                 dest.x, dest.y,
                 dest.w + adj_size(3), dest.h + adj_size(3),
                 get_theme_color(COLOR_THEME_CUSTOM_WIDGET_PRESSED_FRAME));
    break;

  case FC_WS_DISABLED:
    create_frame(icon->dst->surface,
                 dest.x + 1, dest.y + 1,
                 dest.w + adj_size(2), dest.h + adj_size(2),
                 get_theme_color(COLOR_THEME_WIDGET_DISABLED_TEXT));
    break;
  case FC_WS_NORMAL:
    /* No frame by default */
    break;
  }

  dest.x += adj_size(2);
  dest.y += adj_size(2);
  ret = alphablit(icon->theme, NULL, icon->dst->surface, &dest, 255);
  if (ret) {
    return ret;
  }

  return 0;
}

/**********************************************************************//**
  Set new theme and callculate new size.
**************************************************************************/
void set_new_icon_theme(struct widget *icon_widget, SDL_Surface *new_theme)
{
  if ((new_theme) && (icon_widget)) {
    FREESURFACE(icon_widget->theme);
    icon_widget->theme = new_theme;
    icon_widget->size.w = new_theme->w / 4;
    icon_widget->size.h = new_theme->h;
  }
}

/**********************************************************************//**
  Ugly hack to create 4-state icon theme from static icon.
**************************************************************************/
SDL_Surface *create_icon_theme_surf(SDL_Surface *icon)
{
  SDL_Color bg_color = { 255, 255, 255, 128 };
  SDL_Rect dest;
  SDL_Rect src;
  SDL_Surface *ptheme;

  get_smaller_surface_rect(icon, &src);
  ptheme = create_surf((src.w + adj_size(4)) * 4, src.h + adj_size(4),
                       SDL_SWSURFACE);

  dest.x = adj_size(2);
  dest.y = (ptheme->h - src.h) / 2;

  /* normal */
  alphablit(icon, &src, ptheme, &dest, 255);

  /* selected */
  dest.x += (src.w + adj_size(4));
  alphablit(icon, &src, ptheme, &dest, 255);

  /* draw selected frame */
  create_frame(ptheme,
               dest.x - 1, dest.y - 1, src.w + 1, src.h + 1,
               get_theme_color(COLOR_THEME_CUSTOM_WIDGET_SELECTED_FRAME));

  /* pressed */
  dest.x += (src.w + adj_size(4));
  alphablit(icon, &src, ptheme, &dest, 255);

  /* draw selected frame */
  create_frame(ptheme,
               dest.x - 1, dest.y - 1, src.w + 1, src.h + 1,
               get_theme_color(COLOR_THEME_CUSTOM_WIDGET_SELECTED_FRAME));
  /* draw press frame */
  create_frame(ptheme,
               dest.x - adj_size(2), dest.y - adj_size(2),
               src.w + adj_size(3), src.h + adj_size(3),
               get_theme_color(COLOR_THEME_CUSTOM_WIDGET_PRESSED_FRAME));

  /* disabled */
  dest.x += (src.w + adj_size(4));
  alphablit(icon, &src, ptheme, &dest, 255);
  dest.w = src.w;
  dest.h = src.h;

  fill_rect_alpha(ptheme, &dest, &bg_color);

  return ptheme;
}

/**********************************************************************//**
  Create ( malloc ) Icon Widget ( flat Button )
**************************************************************************/
struct widget *create_themeicon(SDL_Surface *icon_theme,
                                struct gui_layer *pdest,
                                Uint32 flags)
{
  struct widget *icon_widget = widget_new();

  icon_widget->theme = icon_theme;

  set_wflag(icon_widget, (WF_FREE_STRING | WF_FREE_GFX | flags));
  set_wstate(icon_widget, FC_WS_DISABLED);
  set_wtype(icon_widget, WT_ICON);
  icon_widget->mod = KMOD_NONE;
  icon_widget->dst = pdest;

  baseclass_redraw = icon_widget->redraw;
  icon_widget->redraw = redraw_icon;

  if (icon_theme) {
    icon_widget->size.w = icon_theme->w / 4;
    icon_widget->size.h = icon_theme->h;
  }

  return icon_widget;
}

/**********************************************************************//**
  Draw the icon.
**************************************************************************/
int draw_icon(struct widget *icon, Sint16 start_x, Sint16 start_y)
{
  icon->size.x = start_x;
  icon->size.y = start_y;

  if (get_wflags(icon) & WF_RESTORE_BACKGROUND) {
    refresh_widget_background(icon);
  }

  return draw_icon_from_theme(icon->theme, get_wstate(icon), icon->dst,
                              icon->size.x, icon->size.y);
}

/**********************************************************************//**
  Blit Icon image to pdest(ination) on position
  start_x, start_y. WARNING: pdest must exist.

  Graphic is taken from icon_theme surface.

  Type of Icon depend of "state" parameter.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled

  Function return: -3 if icon_theme is NULL.
  std return of alphablit(...) function.
**************************************************************************/
int draw_icon_from_theme(SDL_Surface *icon_theme, Uint8 state,
                         struct gui_layer *pdest, Sint16 start_x, Sint16 start_y)
{
  SDL_Rect src, des = {.x = start_x, .y = start_y, .w = 0, .h = 0};

  if (icon_theme == NULL) {
    return -3;
  }

  src.x = 0 + (icon_theme->w / 4) * state;
  src.y = 0;
  src.w = icon_theme->w / 4;
  src.h = icon_theme->h;

  return alphablit(icon_theme, &src, pdest->surface, &des, 255);
}

/**********************************************************************//**
  Create Icon image then return pointer to this image.

  Graphic is take from icon_theme surface and blit to new created image.

  Type of Icon depend of "state" parameter.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled

  Function return NULL if icon_theme is NULL or blit fail.
**************************************************************************/
SDL_Surface *create_icon_from_theme(SDL_Surface *icon_theme, Uint8 state)
{
  SDL_Rect src;

  if (icon_theme == NULL) {
    return NULL;
  }

  src.w = icon_theme->w / 4;
  src.x = src.w * state;
  src.y = 0;
  src.h = icon_theme->h;

  return crop_rect_from_surface(icon_theme, &src);
}

/* =================================================== */
/* ===================== ICON2 ======================= */
/* =================================================== */

/**********************************************************************//**
  Set new theme and calculate new size.
**************************************************************************/
void set_new_icon2_theme(struct widget *icon_widget, SDL_Surface *new_theme,
                         bool free_old_theme)
{
  if ((new_theme) && (icon_widget)) {
    if (free_old_theme) {
      FREESURFACE(icon_widget->theme);
    }
    icon_widget->theme = new_theme;
    icon_widget->size.w = new_theme->w + adj_size(4);
    icon_widget->size.h = new_theme->h + adj_size(4);
  }
}

/**********************************************************************//**
  Create ( malloc ) Icon2 Widget ( flat Button )
**************************************************************************/
struct widget *create_icon2(SDL_Surface *icon, struct gui_layer *pdest,
                            Uint32 flags)
{
  struct widget *icon_widget = widget_new();

  icon_widget->theme = icon;

  set_wflag(icon_widget, (WF_FREE_STRING | WF_FREE_GFX | flags));
  set_wstate(icon_widget, FC_WS_DISABLED);
  set_wtype(icon_widget, WT_ICON2);
  icon_widget->mod = KMOD_NONE;
  icon_widget->dst = pdest;

  baseclass_redraw = icon_widget->redraw;
  icon_widget->redraw = redraw_icon2;

  if (icon) {
    icon_widget->size.w = icon->w + adj_size(4);
    icon_widget->size.h = icon->h + adj_size(4);
  }

  return icon_widget;
}
