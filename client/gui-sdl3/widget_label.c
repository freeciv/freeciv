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
#include "mem.h"

/* client/gui-sdl3 */
#include "colors.h"
#include "graphics.h"
#include "themespec.h"

#include "widget.h"
#include "widget_p.h"

static int (*baseclass_redraw)(struct widget *pwidget);

/**********************************************************************//**
  Blit themelabel2 gfx to surface its on.
**************************************************************************/
static inline int redraw_themelabel2(struct widget *label)
{
  SDL_Rect src = {0,0, label->size.w, label->size.h};
  SDL_Rect dst = {label->size.x, label->size.y, 0, 0};
/*
  if (!label) {
    return -3;
  }
*/
  if (get_wstate(label) == FC_WS_SELECTED) {
    src.y = label->size.h;
  }

  return alphablit(label->theme, &src, label->dst->surface, &dst, 255);
}

/**********************************************************************//**
  Blit label gfx to surface its on.
**************************************************************************/
static int redraw_label(struct widget *label)
{
  int ret;
  SDL_Rect area = label->size;
  SDL_Color bar_color = *get_theme_color(COLOR_THEME_LABEL_BAR);
  SDL_Color backup_color = {0, 0, 0, 0};

  ret = (*baseclass_redraw)(label);
  if (ret != 0) {
    return ret;
  }

  if (get_wtype(label) == WT_T2_LABEL) {
    return redraw_themelabel2(label);
  }

  /* Redraw selected bar */
  if (get_wstate(label) == FC_WS_SELECTED) {
    if (get_wflags(label) & WF_SELECT_WITHOUT_BAR) {
      if (label->string_utf8 != NULL) {
        backup_color = label->string_utf8->fgcol;
        label->string_utf8->fgcol = bar_color;
        if (label->string_utf8->style & TTF_STYLE_BOLD) {
          label->string_utf8->style |= TTF_STYLE_UNDERLINE;
        } else {
          label->string_utf8->style |= TTF_STYLE_BOLD;
        }
      }
    } else {
      fill_rect_alpha(label->dst->surface, &area, &bar_color);
    }
  }

  /* Redraw icon label */
  ret = redraw_iconlabel(label);

  if ((get_wstate(label) == FC_WS_SELECTED) && (label->string_utf8 != NULL)) {
    if (get_wflags(label) & WF_SELECT_WITHOUT_BAR) {
      if (label->string_utf8->style & TTF_STYLE_UNDERLINE) {
        label->string_utf8->style &= ~TTF_STYLE_UNDERLINE;
      } else {
        label->string_utf8->style &= ~TTF_STYLE_BOLD;
      }
      label->string_utf8->fgcol = backup_color;
    } else {
      if (label->string_utf8->render == 3) {
        label->string_utf8->bgcol = backup_color;
      }
    }
  }

  return ret;
}

/**********************************************************************//**
  Calculate new size for a label.
**************************************************************************/
void remake_label_size(struct widget *label)
{
  SDL_Surface *icon = label->theme;
  utf8_str *text = label->string_utf8;
  Uint32 flags = get_wflags(label);
  SDL_Rect buf = { 0, 0, 0, 0 };
  Uint16 w = 0, h = 0, space;

  if (flags & WF_DRAW_TEXT_LABEL_WITH_SPACE) {
    space = adj_size(10);
  } else {
    space = 0;
  }

  if (text != NULL) {
    bool without_box = ((get_wflags(label) & WF_SELECT_WITHOUT_BAR) == WF_SELECT_WITHOUT_BAR);
    bool bold = TRUE;

    if (without_box) {
      bold = ((text->style & TTF_STYLE_BOLD) == TTF_STYLE_BOLD);
      text->style |= TTF_STYLE_BOLD;
    }

    utf8_str_size(text, &buf);

    if (without_box && !bold) {
      text->style &= ~TTF_STYLE_BOLD;
    }

    w = MAX(w, buf.w + space);
    h = MAX(h, buf.h);
  }

  if (icon) {
    if (text != NULL) {
      if ((flags & WF_ICON_UNDER_TEXT) || (flags & WF_ICON_ABOVE_TEXT)) {
        w = MAX(w, icon->w + space);
        h = MAX(h, buf.h + icon->h + adj_size(3));
      } else {
        if (flags & WF_ICON_CENTER) {
          w = MAX(w, icon->w + space);
          h = MAX(h, icon->h);
        } else {
          w = MAX(w, buf.w + icon->w + adj_size(5) + space);
          h = MAX(h, icon->h);
        }
      }
      /* Text */
    } else {
      w = MAX(w, icon->w + space);
      h = MAX(h, icon->h);
    }
  }

  /* Icon */
  label->size.w = w;
  label->size.h = h;
}

/**********************************************************************//**
  Theme label is utf8_str with Background ( icon ).
**************************************************************************/
struct widget *create_themelabel(SDL_Surface *icon, struct gui_layer *pdest,
                                 utf8_str *pstr, Uint16 w, Uint16 h,
                                 Uint32 flags)
{
  struct widget *label = NULL;

  if (icon == NULL && pstr == NULL) {
    return NULL;
  }

  label = widget_new();
  label->theme = icon;
  label->string_utf8 = pstr;
  set_wflag(label,
            (WF_ICON_CENTER | WF_FREE_STRING | WF_FREE_GFX |
             WF_RESTORE_BACKGROUND | flags));
  set_wstate(label, FC_WS_DISABLED);
  set_wtype(label, WT_T_LABEL);
  label->mod = SDL_KMOD_NONE;
  label->dst = pdest;

  baseclass_redraw = label->redraw;
  label->redraw = redraw_label;

  remake_label_size(label);

  label->size.w = MAX(label->size.w, w);
  label->size.h = MAX(label->size.h, h);

  return label;
}

/**********************************************************************//**
  This Label is UTF8 string with Icon.
**************************************************************************/
struct widget *create_iconlabel(SDL_Surface *icon, struct gui_layer *pdest,
                                utf8_str *pstr, Uint32 flags)
{
  struct widget *icon_label = NULL;

  icon_label = widget_new();

  icon_label->theme = icon;
  icon_label->string_utf8 = pstr;
  set_wflag(icon_label, WF_FREE_STRING | WF_FREE_GFX | flags);
  set_wstate(icon_label, FC_WS_DISABLED);
  set_wtype(icon_label, WT_I_LABEL);
  icon_label->mod = SDL_KMOD_NONE;
  icon_label->dst = pdest;

  baseclass_redraw = icon_label->redraw;
  icon_label->redraw = redraw_label;

  remake_label_size(icon_label);

  return icon_label;
}

/**********************************************************************//**
  Theme label is UTF8 string with Background ( icon ).
**************************************************************************/
struct widget *create_themelabel2(SDL_Surface *icon, struct gui_layer *pdest,
                                  utf8_str *pstr, Uint16 w, Uint16 h,
                                  Uint32 flags)
{
  struct widget *label = NULL;
  SDL_Surface *ptheme = NULL;
  SDL_Rect area;
  SDL_Color store = {0, 0, 0, 0};
  SDL_Color bg_color = *get_theme_color(COLOR_THEME_THEMELABEL2_BG);
  Uint32 colorkey;
  const struct SDL_PixelFormatDetails *details;

  if (icon == NULL && pstr == NULL) {
    return NULL;
  }

  label = widget_new();
  label->theme = icon;
  label->string_utf8 = pstr;
  set_wflag(label, (WF_FREE_THEME | WF_FREE_STRING | WF_FREE_GFX | flags));
  set_wstate(label, FC_WS_DISABLED);
  set_wtype(label, WT_T2_LABEL);
  label->mod = SDL_KMOD_NONE;
  baseclass_redraw = label->redraw;
  label->redraw = redraw_label;

  remake_label_size(label);

  label->size.w = MAX(label->size.w, w);
  label->size.h = MAX(label->size.h, h);

  ptheme = create_surf(label->size.w, label->size.h * 2);

  details = SDL_GetPixelFormatDetails(ptheme->format);
  colorkey = SDL_MapRGBA(details, NULL,
                         pstr->bgcol.r, pstr->bgcol.g, pstr->bgcol.b,
                         pstr->bgcol.a);
  SDL_FillSurfaceRect(ptheme, NULL, colorkey);

  label->size.x = 0;
  label->size.y = 0;
  area = label->size;
  label->dst = gui_layer_new(0, 0, ptheme);

  /* Normal */
  redraw_iconlabel(label);

  /* Selected */
  area.x = 0;
  area.y = label->size.h;

  if (flags & WF_RESTORE_BACKGROUND) {
    SDL_FillSurfaceRect(ptheme, &area,
                        map_rgba_details(details, bg_color));
    store = pstr->bgcol;
    SDL_GetRGBA(get_pixel(ptheme, area.x , area.y),
                details, NULL,
                &pstr->bgcol.r, &pstr->bgcol.g,
                &pstr->bgcol.b, &pstr->bgcol.a);
  } else {
    fill_rect_alpha(ptheme, &area, &bg_color);
  }

  label->size.y = label->size.h;
  redraw_iconlabel(label);

  if (flags & WF_RESTORE_BACKGROUND) {
    pstr->bgcol = store;
  }

  label->size.x = 0;
  label->size.y = 0;
  if (flags & WF_FREE_THEME) {
    FREESURFACE(label->theme);
  }
  label->theme = ptheme;
  FC_FREE(label->dst);
  label->dst = pdest;

  return label;
}

/**********************************************************************//**
  Make themeiconlabel2 widget out of iconlabel widget.
**************************************************************************/
struct widget *convert_iconlabel_to_themeiconlabel2(struct widget *icon_label)
{
  SDL_Rect start, area;
  SDL_Color store = {0, 0, 0, 0};
  SDL_Color bg_color = *get_theme_color(COLOR_THEME_THEMELABEL2_BG);
  Uint32 colorkey, flags = get_wflags(icon_label);
  SDL_Surface *pdest;
  SDL_Surface *ptheme = create_surf(icon_label->size.w,
                                    icon_label->size.h * 2);
  const SDL_PixelFormatDetails *details
    = SDL_GetPixelFormatDetails(ptheme->format);

  colorkey = SDL_MapRGBA(details, NULL,
                         icon_label->string_utf8->bgcol.r,
                         icon_label->string_utf8->bgcol.g,
                         icon_label->string_utf8->bgcol.b,
                         icon_label->string_utf8->bgcol.a);
  SDL_FillSurfaceRect(ptheme, NULL, colorkey);

  start = icon_label->size;
  icon_label->size.x = 0;
  icon_label->size.y = 0;
  area = start;
  pdest = icon_label->dst->surface;
  icon_label->dst->surface = ptheme;

  /* Normal */
  redraw_iconlabel(icon_label);

  /* Selected */
  area.x = 0;
  area.y = icon_label->size.h;

  if (flags & WF_RESTORE_BACKGROUND) {
    SDL_FillSurfaceRect(ptheme, &area,
                        map_rgba_details(details, bg_color));
    store = icon_label->string_utf8->bgcol;
    SDL_GetRGBA(get_pixel(ptheme, area.x , area.y),
                details, NULL,
                &icon_label->string_utf8->bgcol.r,
                &icon_label->string_utf8->bgcol.g,
                &icon_label->string_utf8->bgcol.b,
                &icon_label->string_utf8->bgcol.a);
  } else {
    fill_rect_alpha(ptheme, &area, &bg_color);
  }

  icon_label->size.y = icon_label->size.h;
  redraw_iconlabel(icon_label);

  if (flags & WF_RESTORE_BACKGROUND) {
    icon_label->string_utf8->bgcol = store;
  }

  icon_label->size = start;
  if (flags & WF_FREE_THEME) {
    FREESURFACE(icon_label->theme);
  }
  icon_label->theme = ptheme;
  if (flags & WF_FREE_STRING) {
    FREEUTF8STR(icon_label->string_utf8);
  }
  icon_label->dst->surface = pdest;
  set_wtype(icon_label, WT_T2_LABEL);

  icon_label->redraw = redraw_label;

  return icon_label;
}

/**********************************************************************//**
  Blit iconlabel gfx to surface its on.
**************************************************************************/
int redraw_iconlabel(struct widget *label)
{
  int space, ret = 0; /* FIXME: possibly uninitialized */
  Sint16 x, xI, yI;
  Sint16 y = 0; /* FIXME: possibly uninitialized */
  SDL_Surface *text;
  SDL_Rect dst;
  Uint32 flags;

  if (label == NULL) {
    return -3;
  }

  SDL_SetSurfaceClipRect(label->dst->surface, &label->size);

  flags = get_wflags(label);

  if (flags & WF_DRAW_TEXT_LABEL_WITH_SPACE) {
    space = adj_size(5);
  } else {
    space = 0;
  }

  text = create_text_surf_from_utf8(label->string_utf8);

  if (label->theme) { /* Icon */
    if (text) {
      if (flags & WF_ICON_CENTER_RIGHT) {
        xI = label->size.w - label->theme->w - space;
      } else {
        if (flags & WF_ICON_CENTER) {
          xI = (label->size.w - label->theme->w) / 2;
        } else {
          xI = space;
        }
      }

      if (flags & WF_ICON_ABOVE_TEXT) {
        yI = 0;
        y = label->theme->h + adj_size(3)
          + (label->size.h - (label->theme->h + adj_size(3)) - text->h) / 2;
      } else {
        if (flags & WF_ICON_UNDER_TEXT) {
          y = (label->size.h - (label->theme->h + adj_size(3)) - text->h) / 2;
          yI = y + text->h + adj_size(3);
        } else {
          yI = (label->size.h - label->theme->h) / 2;
          y = (label->size.h - text->h) / 2;
        }
      }
      /* Text */
    } else {
#if 0
      yI = (label->size.h - label->theme->h) / 2;
      xI = (label->size.w - label->theme->w) / 2;
#endif /* 0 */
      yI = 0;
      xI = space;
    }

    dst.x = label->size.x + xI;
    dst.y = label->size.y + yI;

    ret = alphablit(label->theme, NULL, label->dst->surface, &dst, 255);

    if (ret) {
      return ret - 10;
    }
  }

  if (text) {
    if (label->theme) { /* Icon */
      if (!(flags & WF_ICON_ABOVE_TEXT) && !(flags & WF_ICON_UNDER_TEXT)) {
        if (flags & WF_ICON_CENTER_RIGHT) {
          if (label->string_utf8->style & SF_CENTER) {
            x = (label->size.w - (label->theme->w + 5 + space) -
                 text->w) / 2;
          } else {
            if (label->string_utf8->style & SF_CENTER_RIGHT) {
              x = label->size.w - (label->theme->w + 5 + space) - text->w;
            } else {
              x = space;
            }
          }
          /* WF_ICON_CENTER_RIGHT */
        } else {
          if (flags & WF_ICON_CENTER) {
            /* text is blit on icon */
            goto alone;
          } else { /* WF_ICON_CENTER_LEFT */
            if (label->string_utf8->style & SF_CENTER) {
              x = space + label->theme->w + adj_size(5) + ((label->size.w -
                                                             (space +
                                                              label->theme->w + adj_size(5)) -
                                                             text->w) / 2);
            } else {
              if (label->string_utf8->style & SF_CENTER_RIGHT) {
                x = label->size.w - text->w - space;
              } else {
                x = space + label->theme->w + adj_size(5);
              }
            }
          } /* WF_ICON_CENTER_LEFT */
        }
        /* !WF_ICON_ABOVE_TEXT && !WF_ICON_UNDER_TEXT */
      } else {
        goto alone;
      }
      /* label->theme == Icon */
    } else {
      y = (label->size.h - text->h) / 2;

    alone:
      if (label->string_utf8->style & SF_CENTER) {
        x = (label->size.w - text->w) / 2;
      } else {
        if (label->string_utf8->style & SF_CENTER_RIGHT) {
          x = label->size.w - text->w - space;
        } else {
          x = space;
        }
      }
    }

    dst.x = label->size.x + x;
    dst.y = label->size.y + y;

    ret = alphablit(text, NULL, label->dst->surface, &dst, 255);
    FREESURFACE(text);
  }

  SDL_SetSurfaceClipRect(label->dst->surface, NULL);

  return ret;
}

/**********************************************************************//**
  Draw the label widget.
**************************************************************************/
int draw_label(struct widget *label, Sint16 start_x, Sint16 start_y)
{
  label->size.x = start_x;
  label->size.y = start_y;

  return redraw_label(label);
}
