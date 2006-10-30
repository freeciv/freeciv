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

#include "widget.h"

/* =================================================== */
/* ===================== VSCROOLBAR ================== */
/* =================================================== */

/*
 */
/**************************************************************************
  Create background image for vscrollbars
  then return pointer to this image.

  Graphic is taken from pVert_theme surface and blit to new created image.

  hight depend of 'High' parametr.

  Type of image depend of "state" parametr.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled
**************************************************************************/
static SDL_Surface *create_vertical_surface(SDL_Surface * pVert_theme,
					    Uint8 state, Uint16 High)
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

  /* tile_len_midd = pVert_theme->h/4  - tile_len_end*2; */
  tile_len_midd = pVert_theme->h / 4 - tile_len_end * 2;

  tile_count_midd = (High - tile_len_end * 2) / tile_len_midd;

  /* correction */
  if (tile_len_midd * tile_count_midd + tile_len_end * 2 < High)
    tile_count_midd++;

  if (!tile_count_midd) {
    pVerSurf = create_surf_alpha(pVert_theme->w, tile_len_end * 2, SDL_SWSURFACE);
  } else {
    pVerSurf = create_surf_alpha(pVert_theme->w, High, SDL_SWSURFACE);
  }

  src.x = 0;
  src.y = start_y;
  src.w = pVert_theme->w;
  src.h = tile_len_end;
  alphablit(pVert_theme, &src, pVerSurf, NULL);

  src.y = start_y + tile_len_end;
  src.h = tile_len_midd;

  des.x = 0;

  for (i = 0; i < tile_count_midd; i++) {
    des.y = tile_len_end + i * tile_len_midd;
    alphablit(pVert_theme, &src, pVerSurf, &des);
  }

  src.y = start_y + tile_len_end + tile_len_midd;
  src.h = tile_len_end;
  des.y = pVerSurf->h - tile_len_end;
  alphablit(pVert_theme, &src, pVerSurf, &des);

  return pVerSurf;
}

/**************************************************************************
  Create ( malloc ) VSrcrollBar Widget structure.

  Theme graphic is taken from pVert_theme surface;

  This function determinate future size of VScrollBar
  ( width = 'pVert_theme->w' , high = 'high' ) and
  save this in: pWidget->size rectangle ( SDL_Rect )

  function return pointer to allocated Widget.
**************************************************************************/
struct widget * create_vertical(SDL_Surface *pVert_theme, SDL_Surface *pDest,
  			Uint16 high, Uint32 flags)
{
  struct widget *pVer = fc_calloc(1, sizeof(struct widget));

  pVer->theme = pVert_theme;
  pVer->size.w = pVert_theme->w;
  pVer->size.h = high;
  set_wflag(pVer, (WF_FREE_STRING | flags));
  set_wstate(pVer, FC_WS_DISABLED);
  set_wtype(pVer, WT_VSCROLLBAR);
  pVer->mod = KMOD_NONE;
  pVer->dst = pDest;

  return pVer;
}

/**************************************************************************
  ...
**************************************************************************/
int redraw_vert(struct widget *pVert)
{
  int ret;
  SDL_Rect dest;
  
  dest = pVert->size;
  fix_rect(pVert->dst, &dest);
  
  SDL_Surface *pVert_Surf = create_vertical_surface(pVert->theme,
						    get_wstate(pVert),
						    pVert->size.h);
  ret =
      blit_entire_src(pVert_Surf, pVert->dst, dest.x, dest.y);
  
  FREESURFACE(pVert_Surf);

  return ret;
}

/**************************************************************************
  ...
**************************************************************************/
int draw_vert(struct widget *pVert, Sint16 x, Sint16 y)
{
  pVert->size.x = x;
  pVert->size.y = y;
  pVert->gfx = crop_rect_from_surface(pVert->dst, &pVert->size);
  return redraw_vert(pVert);
}

/* =================================================== */
/* ===================== HSCROOLBAR ================== */
/* =================================================== */

/**************************************************************************
  Create background image for hscrollbars
  then return	pointer to this image.

  Graphic is taken from pHoriz_theme surface and blit to new created image.

  hight depend of 'Width' parametr.

  Type of image depend of "state" parametr.
    state = 0 - normal
    state = 1 - selected
    state = 2 - pressed
    state = 3 - disabled
**************************************************************************/
static SDL_Surface *create_horizontal_surface(SDL_Surface * pHoriz_theme,
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
    pHorSurf = create_surf_alpha(tile_len_end * 2, pHoriz_theme->h, SDL_SWSURFACE);
  } else {
    pHorSurf = create_surf_alpha(Width, pHoriz_theme->h, SDL_SWSURFACE);
  }

  src.y = 0;
  src.x = start_x;
  src.h = pHoriz_theme->h;
  src.w = tile_len_end;
  alphablit(pHoriz_theme, &src, pHorSurf, NULL);

  src.x = start_x + tile_len_end;
  src.w = tile_len_midd;

  des.y = 0;

  for (i = 0; i < tile_count_midd; i++) {
    des.x = tile_len_end + i * tile_len_midd;
    alphablit(pHoriz_theme, &src, pHorSurf, &des);
  }

  src.x = start_x + tile_len_end + tile_len_midd;
  src.w = tile_len_end;
  des.x = pHorSurf->w - tile_len_end;
  alphablit(pHoriz_theme, &src, pHorSurf, &des);

  return pHorSurf;
}

/**************************************************************************
  Create ( malloc ) VSrcrollBar Widget structure.

  Theme graphic is taken from pVert_theme surface;

  This function determinate future size of VScrollBar
  ( width = 'pVert_theme->w' , high = 'high' ) and
  save this in: pWidget->size rectangle ( SDL_Rect )

  function return pointer to allocated Widget.
**************************************************************************/
struct widget * create_horizontal(SDL_Surface *pHoriz_theme, SDL_Surface *pDest,
  		Uint16 width, Uint32 flags)
{
  struct widget *pHor = fc_calloc(1, sizeof(struct widget));

  pHor->theme = pHoriz_theme;
  pHor->size.w = width;
  pHor->size.h = pHoriz_theme->h;
  set_wflag(pHor, WF_FREE_STRING | flags);
  set_wstate(pHor, FC_WS_DISABLED);
  set_wtype(pHor, WT_HSCROLLBAR);
  pHor->mod = KMOD_NONE;
  pHor->dst = pDest;
  
  return pHor;
}

/**************************************************************************
  ...
**************************************************************************/
int redraw_horiz(struct widget *pHoriz)
{
  SDL_Rect dest;
  
  dest = pHoriz->size;
  fix_rect(pHoriz->dst, &dest);
  
  SDL_Surface *pHoriz_Surf = create_horizontal_surface(pHoriz->theme,
						       get_wstate(pHoriz),
						       pHoriz->size.w);
  int ret = blit_entire_src(pHoriz_Surf, pHoriz->dst, dest.x, dest.y);
  FREESURFACE(pHoriz_Surf);

  return ret;
}

/**************************************************************************
  ...
**************************************************************************/
int draw_horiz(struct widget *pHoriz, Sint16 x, Sint16 y)
{
  pHoriz->size.x = x;
  pHoriz->size.y = y;
  pHoriz->gfx = crop_rect_from_surface(pHoriz->dst, &pHoriz->size);
  return redraw_horiz(pHoriz);
}
