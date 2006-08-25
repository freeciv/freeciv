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

#include "gui_stuff.h"

/* =================================================== */
/* ===================== ICON ======================== */
/* =================================================== */

/**************************************************************************
  set new theme and callculate new size.
**************************************************************************/
void set_new_icon_theme(struct widget *pIcon_Widget, SDL_Surface * pNew_Theme)
{
  if ((pNew_Theme) && (pIcon_Widget)) {
    FREESURFACE(pIcon_Widget->theme);
    pIcon_Widget->theme = pNew_Theme;
    pIcon_Widget->size.w = pNew_Theme->w / 4;
    pIcon_Widget->size.h = pNew_Theme->h;
  }
}

/**************************************************************************
  Ugly hack to create 4-state icon theme from static icon;
**************************************************************************/
SDL_Surface *create_icon_theme_surf(SDL_Surface * pIcon)
{
  SDL_Color bg_color = { 255, 255, 255, 128 };
  
  SDL_Rect dest, src = get_smaller_surface_rect(pIcon);
  SDL_Surface *pTheme = create_surf_alpha((src.w + adj_size(4)) * 4, src.h + adj_size(4),
				    SDL_SWSURFACE);
  dest.x = adj_size(2);
  dest.y = (pTheme->h - src.h) / 2;

  /* normal */
  alphablit(pIcon, &src, pTheme, &dest);

  /* sellected */
  dest.x += (src.w + adj_size(4));
  alphablit(pIcon, &src, pTheme, &dest);
  /* draw sellect frame */
  putframe(pTheme, dest.x - 1, dest.y - 1, dest.x + (src.w), dest.y + src.h,
    map_rgba(pTheme->format, *get_game_colorRGB(COLOR_THEME_CUSTOM_WIDGET_SELECTED_FRAME)));

  /* pressed */
  dest.x += (src.w + adj_size(4));
  alphablit(pIcon, &src, pTheme, &dest);
  /* draw sellect frame */
  putframe(pTheme, dest.x - 1, dest.y - 1, dest.x + src.w, dest.y + src.h,
    map_rgba(pTheme->format, *get_game_colorRGB(COLOR_THEME_CUSTOM_WIDGET_SELECTED_FRAME)));
  /* draw press frame */
  putframe(pTheme, dest.x - adj_size(2), dest.y - adj_size(2), dest.x + (src.w + 1),
	   dest.y + (src.h + 1),
    map_rgba(pTheme->format, *get_game_colorRGB(COLOR_THEME_CUSTOM_WIDGET_PRESSED_FRAME)));

  /* disabled */
  dest.x += (src.w + adj_size(4));
  alphablit(pIcon, &src, pTheme, &dest);
  dest.w = src.w;
  dest.h = src.h;

  SDL_FillRectAlpha(pTheme, &dest, &bg_color);

  return pTheme;
}

/**************************************************************************
  Create ( malloc ) Icon Widget ( flat Button )
**************************************************************************/
struct widget * create_themeicon(SDL_Surface *pIcon_theme, SDL_Surface *pDest,
							  Uint32 flags)
{
  struct widget *pIcon_Widget = fc_calloc(1, sizeof(struct widget));

  pIcon_Widget->theme = pIcon_theme;

  set_wflag(pIcon_Widget, (WF_FREE_STRING | flags));
  set_wstate(pIcon_Widget, FC_WS_DISABLED);
  set_wtype(pIcon_Widget, WT_ICON);
  pIcon_Widget->mod = KMOD_NONE;
  pIcon_Widget->dst = pDest;
  
  if (pIcon_theme) {
    pIcon_Widget->size.w = pIcon_theme->w / 4;
    pIcon_Widget->size.h = pIcon_theme->h;
  }

  return pIcon_Widget;
}

/**************************************************************************
  ...
**************************************************************************/
int draw_icon(struct widget *pIcon, Sint16 start_x, Sint16 start_y)
{
  pIcon->size.x = start_x;
  pIcon->size.y = start_y;

  if (get_wflags(pIcon) & WF_RESTORE_BACKGROUND) {
    refresh_widget_background(pIcon);
  }

  return draw_icon_from_theme(pIcon->theme, get_wstate(pIcon), pIcon->dst,
			      pIcon->size.x, pIcon->size.y);
}

/**************************************************************************
  ...
**************************************************************************/
int real_redraw_icon(struct widget *pIcon)
{
  SDL_Rect src, area = pIcon->size;

  fix_rect(pIcon->dst, &area);
  
  if (!pIcon->theme) {
    return -3;
  }

  if (pIcon->gfx) {
    clear_surface(pIcon->dst, &area);
    alphablit(pIcon->gfx, NULL, pIcon->dst, &area);
  }

  src.x = (pIcon->theme->w / 4) * (Uint8) (get_wstate(pIcon));
  src.y = 0;
  src.w = (pIcon->theme->w / 4);
  src.h = pIcon->theme->h;

  if (pIcon->size.w != src.w) {
    area.x += (pIcon->size.w - src.w) / 2;
  }

  if (pIcon->size.h != src.h) {
    area.y += (pIcon->size.h - src.h) / 2;
  }

  return alphablit(pIcon->theme, &src, pIcon->dst, &area);
}

/**************************************************************************
  Blit Icon image to pDest(ination) on positon
  start_x , start_y. WARRING: pDest must exist.

  Graphic is taken from pIcon_theme surface.

  Type of Icon depend of "state" parametr.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled

  Function return: -3 if pIcon_theme is NULL. 
  std return of alphablit(...) function.
**************************************************************************/
int draw_icon_from_theme(SDL_Surface * pIcon_theme, Uint8 state,
			 SDL_Surface * pDest, Sint16 start_x, Sint16 start_y)
{
  SDL_Rect src, des = {start_x, start_y, 0, 0};

  fix_rect(pIcon_theme, &des);
  
  if (!pIcon_theme) {
    return -3;
  }
  /* src.x = 0 + pIcon_theme->w/4 * state */
  src.x = 0 + (pIcon_theme->w / 4) * state;
  src.y = 0;
  src.w = pIcon_theme->w / 4;
  src.h = pIcon_theme->h;
  return alphablit(pIcon_theme, &src, pDest, &des);
}

/**************************************************************************
  Create Icon image then return pointer to this image.
  
  Graphic is take from pIcon_theme surface and blit to new created image.

  Type of Icon depend of "state" parametr.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled

  Function return NULL if pIcon_theme is NULL or blit fail. 
**************************************************************************/
SDL_Surface * create_icon_from_theme(SDL_Surface *pIcon_theme, Uint8 state)
{
  SDL_Rect src;
  
  if (!pIcon_theme) {
    return NULL;
  }
  
  src.w = pIcon_theme->w / 4;  
  src.x = src.w * state;
  src.y = 0;
  src.h = pIcon_theme->h;
  
  return crop_rect_from_surface(pIcon_theme, &src);
}

/* =================================================== */
/* ===================== ICON2 ======================= */
/* =================================================== */

/**************************************************************************
  set new theme and callculate new size.
**************************************************************************/
void set_new_icon2_theme(struct widget *pIcon_Widget, SDL_Surface *pNew_Theme,
			  bool free_old_theme)
{
  if ((pNew_Theme) && (pIcon_Widget)) {
    if(free_old_theme) {
      FREESURFACE(pIcon_Widget->theme);
    }
    pIcon_Widget->theme = pNew_Theme;
    pIcon_Widget->size.w = pNew_Theme->w + adj_size(4);
    pIcon_Widget->size.h = pNew_Theme->h + adj_size(4);
  }
}

/**************************************************************************
  Create ( malloc ) Icon2 Widget ( flat Button )
**************************************************************************/
struct widget * create_icon2(SDL_Surface *pIcon, SDL_Surface *pDest, Uint32 flags)
{

  struct widget *pIcon_Widget = fc_calloc(1, sizeof(struct widget));

  pIcon_Widget->theme = pIcon;

  set_wflag(pIcon_Widget, (WF_FREE_STRING | flags));
  set_wstate(pIcon_Widget, FC_WS_DISABLED);
  set_wtype(pIcon_Widget, WT_ICON2);
  pIcon_Widget->mod = KMOD_NONE;
  pIcon_Widget->dst = pDest;
  
  if (pIcon) {
    pIcon_Widget->size.w = pIcon->w + adj_size(4);
    pIcon_Widget->size.h = pIcon->h + adj_size(4);
  }

  return pIcon_Widget;
}

/**************************************************************************
  ...
**************************************************************************/
int real_redraw_icon2(struct widget *pIcon)
{
  int ret;
  SDL_Rect dest = pIcon->size;
  Uint32 state;

  if(!pIcon) {
    return -3;
  }
  
  if(!pIcon->theme) {
    return -4;
  }
  
  fix_rect(pIcon->dst, &dest);
  
  state = get_wstate(pIcon);
    
  if (pIcon->gfx) {
    clear_surface(pIcon->dst, &dest);
    ret = alphablit(pIcon->gfx, NULL, pIcon->dst, &dest);
    if (ret) {
      return ret;
    }
  }

  dest.x = pIcon->size.x;
  dest.y = pIcon->size.y;
  dest.w = pIcon->theme->w;
  dest.h = pIcon->theme->h;
  fix_rect(pIcon->dst, &dest);  

  if (state == FC_WS_SELLECTED) {
    putframe(pIcon->dst, dest.x + 1, dest.y + 1,
      dest.x + dest.w + adj_size(2), dest.y + dest.h + adj_size(2),
      map_rgba(pIcon->dst->format, *get_game_colorRGB(COLOR_THEME_CUSTOM_WIDGET_SELECTED_FRAME)));
  }

  if (state == FC_WS_PRESSED) {
    putframe(pIcon->dst, dest.x + 1, dest.y + 1,
      dest.x + dest.w + adj_size(2), dest.y + dest.h + adj_size(2),
      map_rgba(pIcon->dst->format, *get_game_colorRGB(COLOR_THEME_CUSTOM_WIDGET_SELECTED_FRAME)));

    putframe(pIcon->dst, dest.x, dest.y,
	     dest.x + dest.w + adj_size(3), dest.y + dest.h + adj_size(3),
	     map_rgba(pIcon->dst->format, *get_game_colorRGB(COLOR_THEME_CUSTOM_WIDGET_PRESSED_FRAME)));
  }

  if (state == FC_WS_DISABLED) {
    putframe(pIcon->dst, dest.x + 1, dest.y + 1,
	     dest.x + dest.w + adj_size(2), dest.y + dest.h + adj_size(2),
	     map_rgba(pIcon->dst->format, *get_game_colorRGB(COLOR_THEME_WIDGET_DISABLED_TEXT)));
  }
  dest.x += adj_size(2);
  dest.y += adj_size(2);
  ret = alphablit(pIcon->theme, NULL, pIcon->dst, &dest);
  if (ret) {
    return ret;
  }
    
  return 0;
}
