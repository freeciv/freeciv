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
#include "gui_tilespec.h"
#include "themespec.h"

#include "widget.h"
#include "widget_p.h"

static int (*checkbox_baseclass_redraw)(struct widget *pwidget);
static int (*textcheckbox_baseclass_redraw)(struct widget *pwidget);

/**********************************************************************//**
  Blit checkbox gfx to surface its on.
**************************************************************************/
static int redraw_checkbox(struct widget *icon)
{
  int ret;
  SDL_Rect src, area = icon->size;

  ret = (*checkbox_baseclass_redraw)(icon);
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
  Blit checkbox-with-text gfx to surface its on.

  @return 0 on success
**************************************************************************/
static int redraw_textcheckbox(struct widget *cbox)
{
  int ret;
  SDL_Surface *theme_surface, *icon;

  ret = (*textcheckbox_baseclass_redraw)(cbox);
  if (ret != 0) {
    return ret;
  }

  if (cbox->string_utf8 == NULL) {
    return widget_redraw(cbox);
  }

  theme_surface = cbox->theme;
  icon = create_icon_from_theme(theme_surface, get_wstate(cbox));

  if (!icon) {
    return -3; /* FIXME: What SDL_Error this maps to, and should it map to none?
                *        Maybe it should be a positive value (SDL_Errors have
                *        negative values space) */
  }

  cbox->theme = icon;

  /* redraw icon label */
  ret = redraw_iconlabel(cbox);

  FREESURFACE(icon);
  cbox->theme = theme_surface;

  return ret;
}

/**********************************************************************//**
  Create a new checkbox widget.
**************************************************************************/
struct widget *create_checkbox(struct gui_layer *pdest, bool state,
                               Uint32 flags)
{
  struct widget *cbox = widget_new();
  struct checkbox *ptmp = fc_calloc(1, sizeof(struct checkbox));

  if (state) {
    cbox->theme = current_theme->cbox_sell_icon;
  } else {
    cbox->theme = current_theme->cbox_unsell_icon;
  }

  set_wflag(cbox, (WF_FREE_STRING | WF_FREE_GFX | WF_FREE_PRIVATE_DATA | flags));
  set_wstate(cbox, FC_WS_DISABLED);
  set_wtype(cbox, WT_CHECKBOX);
  cbox->mod = SDL_KMOD_NONE;
  cbox->dst = pdest;
  ptmp->state = state;
  ptmp->true_theme = current_theme->cbox_sell_icon;
  ptmp->false_theme = current_theme->cbox_unsell_icon;
  cbox->private_data.cbox = ptmp;

  checkbox_baseclass_redraw = cbox->redraw;
  cbox->redraw = redraw_checkbox;

  cbox->size.w = cbox->theme->w / 4;
  cbox->size.h = cbox->theme->h;

  return cbox;
}

/**********************************************************************//**
  Create a new checkbox-with-text widget.
**************************************************************************/
struct widget *create_textcheckbox(struct gui_layer *pdest, bool state,
                                   utf8_str *pstr, Uint32 flags)
{
  struct widget *cbox;
  struct checkbox *ptmp;
  SDL_Surface *surf, *icon;
  struct widget *tmp_widget;

  if (pstr == NULL) {
    return create_checkbox(pdest, state, flags);
  }

  ptmp = fc_calloc(1, sizeof(struct checkbox));

  if (state) {
    surf = current_theme->cbox_sell_icon;
  } else {
    surf = current_theme->cbox_unsell_icon;
  }

  icon = create_icon_from_theme(surf, 0);
  cbox = create_iconlabel(icon, pdest, pstr, (flags | WF_FREE_PRIVATE_DATA));

  pstr->style &= ~SF_CENTER;

  cbox->theme = surf;
  FREESURFACE(icon);

  set_wtype(cbox, WT_TCHECKBOX);
  ptmp->state = state;
  ptmp->true_theme = current_theme->cbox_sell_icon;
  ptmp->false_theme = current_theme->cbox_unsell_icon;
  cbox->private_data.cbox = ptmp;

  tmp_widget = widget_new();
  /* We can't use cbox->redraw here, because it is of type iconlabel */
  textcheckbox_baseclass_redraw = tmp_widget->redraw;
  FREEWIDGET(tmp_widget);
  cbox->redraw = redraw_textcheckbox;

  return cbox;
}

/**********************************************************************//**
  Set theme surfaces for a checkbox.
**************************************************************************/
int set_new_checkbox_theme(struct widget *cbox,
                           SDL_Surface *true_surf, SDL_Surface *false_surf)
{
  struct checkbox *ptmp;

  if (!cbox || (get_wtype(cbox) != WT_CHECKBOX)) {
    return -1;
  }

  if (!cbox->private_data.cbox) {
    cbox->private_data.cbox = fc_calloc(1, sizeof(struct checkbox));
    set_wflag(cbox, WF_FREE_PRIVATE_DATA);
    cbox->private_data.cbox->state = FALSE;
  }

  ptmp = cbox->private_data.cbox;
  ptmp->true_theme = true_surf;
  ptmp->false_theme = false_surf;
  if (ptmp->state) {
    cbox->theme = true_surf;
  } else {
    cbox->theme = false_surf;
  }

  return 0;
}

/**********************************************************************//**
  Change the state of the checkbox.
**************************************************************************/
void toggle_checkbox(struct widget *cbox)
{
  if (cbox->private_data.cbox->state) {
    cbox->theme = cbox->private_data.cbox->false_theme;
    cbox->private_data.cbox->state = FALSE;
  } else {
    cbox->theme = cbox->private_data.cbox->true_theme;
    cbox->private_data.cbox->state = TRUE;
  }
}

/**********************************************************************//**
  Check state of the checkbox.
**************************************************************************/
bool get_checkbox_state(struct widget *cbox)
{
  return cbox->private_data.cbox->state;
}
