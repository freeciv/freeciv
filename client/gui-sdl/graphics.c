/********************************************************************** 
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

/**********************************************************************
                          graphics.c  -  description
                             -------------------
    begin                : Mon Jul 1 2002
    copyright            : (C) 2000 by Michael Speck
			 : (C) 2002 by Rafa³ Bursig
    email                : Michael Speck <kulkanie@gmx.net>
			 : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

/**************************************************************************
  Idea of "putline" code came from SDL_gfx lib. 
**************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*#include <stdio.h>*/
#include <stdlib.h>
#include <string.h>
/*#include <ctype.h>*/

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <SDL/SDL_image.h>


#include "game.h"

#include "fcintl.h"
#include "log.h"

#include "gui_mem.h"

#include "shared.h"
#include "support.h"
#include "unit.h"
#include "version.h"

#include "climisc.h"
#include "colors.h"

#include "gui_iconv.h"

#include "mapview_g.h"
#include "options.h"
#include "tilespec.h"

#include "graphics.h"

#include "gui_main.h"

#include "goto_cursor.xbm"
#include "goto_cursor_mask.xbm"
#include "drop_cursor.xbm"
#include "drop_cursor_mask.xbm"
#include "nuke_cursor.xbm"
#include "nuke_cursor_mask.xbm"
#include "patrol_cursor.xbm"
#include "patrol_cursor_mask.xbm"

struct Sdl Main;

/* SDL_Surface	*pIntro_gfx; */
static char *pIntro_gfx_path;
static char *pLogo_gfx_path;
static char *pCity_gfx_path;

SDL_Cursor *pStd_Cursor;
SDL_Cursor *pGoto_Cursor;
SDL_Cursor *pDrop_Cursor;
SDL_Cursor *pNuke_Cursor;
SDL_Cursor *pPatrol_Cursor;

static SDL_Cursor *init_cursor(const char *image_data,
			       const char *image_mask, int width,
			       int height, int hot_x, int hot_y);

static void DEL_RECTLIST(struct RectList *pRectList);

/* ============ FreeCiv sdl graphics function =========== */

/**************************************************************************
  Convert cursor from "xbm" format to SDL_Cursor format and create them.
**************************************************************************/
static SDL_Cursor *init_cursor(const char *image_data,
			       const char *image_mask, int width,
			       int height, int hot_x, int hot_y)
{
  Uint8 *data;
  Uint8 *mask;
  int i = 0;
  size_t size = height << 2;

  data = ALLOCA(size);
  mask = ALLOCA(size);

  while (i != size) {
    data[i] = image_data[i + 3];
    mask[i] = image_mask[i + 3];
    data[i + 1] = image_data[i + 2];
    mask[i + 1] = image_mask[i + 2];
    data[i + 2] = image_data[i + 1];
    mask[i + 2] = image_mask[i + 1];
    data[i + 3] = image_data[i];
    mask[i + 3] = image_mask[i];
    i += 4;
  }

  return SDL_CreateCursor(data, mask, width, height, hot_x, hot_y);
}

/**************************************************************************
  ...
**************************************************************************/
SDL_Surface *crop_rect_from_surface(SDL_Surface * pSource,
				    SDL_Rect * pRect)
{
  SDL_Surface *pNew = create_surf_with_format(pSource->format,
					      pRect->w, pRect->h,
					      SDL_SWSURFACE);

  if (pSource->format->Amask) {
    SDL_SetAlpha(pSource, 0x0, 0x0);
  }

  if (SDL_BlitSurface(pSource, pRect, pNew, NULL)) {
    FREESURFACE(pNew);
  }

  if (pSource->format->Amask) {
    SDL_SetAlpha(pSource, SDL_SRCALPHA, 255);
  }

  return pNew;
}

/**************************************************************************
  Load a surface from file putting it in software mem.
**************************************************************************/
SDL_Surface *load_surf(const char *pFname)
{
  SDL_Surface *pBuf = NULL;
  SDL_Surface *pNew_sur = NULL;


  if ((pBuf = IMG_Load(pFname)) == NULL) {
    freelog(LOG_ERROR, _("load_surf: £adowanie pliku graficznego %s \
			nie powiod³o siê !!"), pFname);

    return NULL;		/* Should I use abotr() ? */
  }

  if ((pNew_sur = SDL_DisplayFormat(pBuf)) == NULL) {
    freelog(LOG_ERROR, _("load_surf: \
			Nie moge zkonwertowaæ obrazu z pliku %s do formatu \
			ekranu."), pFname);
    return pBuf;
  }

  FREESURFACE(pBuf);

  return pNew_sur;
}

/**************************************************************************
  Load a surface from file putting it in soft or hardware mem
  Warning: pFname must have full path to file
**************************************************************************/
SDL_Surface *load_surf_with_flags(const char *pFname, int iFlags)
{
  SDL_Surface *pBuf = NULL;
  SDL_Surface *pNew_sur = NULL;
  SDL_PixelFormat *pSpf = SDL_GetVideoSurface()->format;

  if ((pBuf = IMG_Load(pFname)) == NULL) {
    freelog(LOG_ERROR, _("load_surf_with_flags: £adowanie pliku \
			graficznego %s wywo³ane	z pliku %s w lini %d nie \
			powiod³o siê !!"), pFname, __FILE__, __LINE__);

    return NULL;		/* Should I use abotr() ? */
  }

  if ((pNew_sur = SDL_ConvertSurface(pBuf, pSpf, iFlags)) == NULL) {
    freelog(LOG_ERROR, _("Plik %s (linia %d): "
			 "Nie moge zkonwertowaæ obrazu z pliku %s "
			 "do formatu %d."),
	    __FILE__, __LINE__, pFname, iFlags);

    return pBuf;
  }

  FREESURFACE(pBuf);

  return pNew_sur;
}

/**************************************************************************
   create an surface with format
   MUST NOT BE USED IF NO SDLSCREEN IS SET
**************************************************************************/
SDL_Surface *create_surf_with_format(SDL_PixelFormat * pSpf,
				     int iWidth, int iHeight,
				     Uint32 iFlags)
{
  SDL_Surface *pSurf = SDL_CreateRGBSurface(iFlags, iWidth, iHeight,
					    pSpf->BitsPerPixel,
					    pSpf->Rmask,
					    pSpf->Gmask,
					    pSpf->Bmask, pSpf->Amask);

  if (!pSurf) {
    freelog(LOG_ERROR, _("Plik %s (linia %d): \
			Nie moge utorzyæ Sprit'a (Surface) o wymiarach \
			%d x %d %d Bits i formatu %d"), __FILE__, __LINE__, iWidth, iHeight, pSpf->BitsPerPixel, iFlags);
    return NULL;
  }

  return pSurf;
}

/**************************************************************************
  create an surface with screen format and fill with color.
  if pColor == NULL surface is filled with transparent white A = 128
  MUST NOT BE USED IF NO SDLSCREEN IS SET
**************************************************************************/
SDL_Surface *create_filled_surface(Uint16 w, Uint16 h, Uint32 iFlags,
				   SDL_Color * pColor)
{
  /* pColor->unused == ALPHA */
  SDL_Surface *pNew = create_surf(w, h, iFlags);
  SDL_Color color;

  if (!pNew) {
    return NULL;
  }

  if (!pColor) {
    pColor = &color;
    pColor->r = 255;
    pColor->g = 255;
    pColor->b = 255;
    pColor->unused = 128;
  }

  SDL_FillRect(pNew, NULL,
	       SDL_MapRGBA(pNew->format, pColor->r, pColor->g, pColor->b,
			   pColor->unused));

  if (pColor->unused != 255) {
    SDL_SetAlpha(pNew, SDL_SRCALPHA, pColor->unused);
  }

  return pNew;
}

/**************************************************************************
  blit entire src [SOURCE] surface to destination [DEST] surface
  on position : [iDest_x],[iDest_y] using it's actual alpha and
  color key settings.
**************************************************************************/
int blit_entire_src(SDL_Surface * pSrc, SDL_Surface * pDest,
		    Sint16 iDest_x, Sint16 iDest_y)
{
  SDL_Rect dest_rect = { iDest_x, iDest_y, 0, 0 };
  return SDL_BlitSurface(pSrc, NULL, pDest, &dest_rect);
}

/**************************************************************************
  get pixel
  Return the pixel value at (x, y)
  NOTE: The surface must be locked before calling this!
**************************************************************************/
Uint32 getpixel(SDL_Surface * pSurface, Sint16 x, Sint16 y)
{
  switch (pSurface->format->BytesPerPixel) {
  case 1:
    return *(Uint8 *) ((Uint8 *) pSurface->pixels + y * pSurface->pitch +
		       x);

  case 2:
    return *(Uint16 *) ((Uint8 *) pSurface->pixels + y * pSurface->pitch +
			(x << 1));

  case 3:
    {
      /* Here ptr is the address to the pixel we want to retrieve */
      Uint8 *ptr =
	  (Uint8 *) pSurface->pixels + y * pSurface->pitch + x * 3;
      if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
	return ptr[0] << 16 | ptr[1] << 8 | ptr[2];
      } else {
	return ptr[0] | ptr[1] << 8 | ptr[2] << 16;
      }
    }
  case 4:
    return *(Uint32 *) ((Uint8 *) pSurface->pixels + y * pSurface->pitch +
			(x << 2));

  default:
    return 0;			/* shouldn't happen, but avoids warnings */
  }
}

/**************************************************************************
  Draw Horizontal line;
  NOTE: Befor call this func. check if 'y' is inside 'pDest' surface.
**************************************************************************/
static void put_vline(SDL_Surface * pDest, int x, Sint16 y0, Sint16 y1,
		      Uint32 color)
{
  register Uint8 *buf_ptr, *max;
  register int pitch = pDest->pitch;
  register size_t lng;

  /* correct x0, x1 position ( must be inside 'pDest' surface ) */
  if (y0 < 0) {
    y0 = 0;
  } else if (y0 > pDest->h) {
    y0 = pDest->h;
  }

  if (y1 < 0) {
    y1 = 0;
  } else if (y1 > pDest->w) {
    y1 = pDest->w;
  }

  lng = (y1 - y0);

  if (lng < 0) {
    int tmp = y0;
    y0 = y1;
    y1 = tmp;
    lng *= -1;
  }

  buf_ptr = ((Uint8 *) pDest->pixels + (y0 * pitch));
  max = buf_ptr + (lng * pitch);

  switch (pDest->format->BytesPerPixel) {
  case 1:
    buf_ptr += x;
    max += x;
    for (; buf_ptr < max; buf_ptr += pitch) {
      *(Uint8 *) buf_ptr = color & 0xFF;
    }
    return;
  case 2:
    buf_ptr += (x << 1);
    max += (x << 1);
    for (; buf_ptr < max; buf_ptr += pitch) {
      *(Uint16 *) buf_ptr = color & 0xFFFF;
    }
    return;
  case 3:
    buf_ptr += (x << 1) + x;
    max += (x << 1) + x;
    for (; buf_ptr < max; buf_ptr += pitch) {
      if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
	buf_ptr[0] = (color >> 16) & 0xff;
	buf_ptr[1] = (color >> 8) & 0xff;
	buf_ptr[2] = color & 0xff;
      } else {
	buf_ptr[0] = color & 0xff;
	buf_ptr[1] = (color >> 8) & 0xff;
	buf_ptr[2] = (color >> 16) & 0xff;
      }
    }
    return;
  case 4:
    buf_ptr += x << 2;
    max += x << 2;
    for (; buf_ptr < max; buf_ptr += pitch) {
      *(Uint32 *) buf_ptr = color;
    }
    return;
  }
}

/**************************************************************************
  Draw Horizontal line;
  NOTE: Befor call this func. check if 'y' is inside 'pDest' surface.
**************************************************************************/
static void put_hline(SDL_Surface * pDest, int y, Sint16 x0, Sint16 x1,
		      Uint32 color)
{
  register Uint8 *buf_ptr = ((Uint8 *) pDest->pixels + (y * pDest->pitch));
  register size_t lng;
  register int i;

  /* correct x0, x1 position ( must be inside 'pDest' surface ) */
  if (x0 < 0) {
    x0 = 0;
  } else if (x0 > pDest->w) {
    x0 = pDest->w;
  }

  if (x1 < 0) {
    x1 = 0;
  } else if (x1 > pDest->w) {
    x1 = pDest->w;
  }

  lng = (x1 - x0);
  if (lng < 0) {
    lng *= -1;
  }

  switch (pDest->format->BytesPerPixel) {
  case 1:
    buf_ptr += x0;
    memset(buf_ptr, (color & 0xff), lng);
    return;
  case 2:
    buf_ptr += (x0 << 1);

    color &= 0xffff;
    color = (color << 16) | color;
    i = 0;

    /*if ( lng - ((lng>>1)<<1) ) */
    if (lng & 0x01) {
      *(Uint16 *) buf_ptr = (color & 0xffff);
      buf_ptr += 2;
      i = 1;
    }

    for (; i < lng; i += 2, buf_ptr += 4) {
      *(Uint32 *) buf_ptr = color;
    }

    return;
  case 3:
    {
      Uint32 color1, color2;
      i = 0;
      buf_ptr += ((x0 << 1) + x0);

      /*if ( lng - ((lng>>2)<<2) ) */
      if (lng & 0x03) {
	for (; i < (lng & 0x03); i++) {
	  if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
	    buf_ptr[0] = (color >> 16) & 0xff;
	    buf_ptr[1] = (color >> 8) & 0xff;
	    buf_ptr[2] = color & 0xff;
	  } else {
	    buf_ptr[0] = (color & 0xff);
	    buf_ptr[1] = (color >> 8) & 0xff;
	    buf_ptr[2] = (color >> 16) & 0xff;
	  }
	  buf_ptr += 3;
	}
	i = lng & 0x03;
      }

      if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
	color2 = ((color >> 16) & 0xff) |
	    (color & 0xff00) |
	    ((color << 16) & 0xff0000) | ((color << 8) & 0xff000000);

	color1 = ((color >> 8) & 0xff) |
	    ((color << 8) & 0xff00) |
	    (color & 0xff0000) | ((color << 16) & 0xff000000);

	color = (color & 0xff) |
	    ((color >> 8) & 0xff00) |
	    ((color << 8) & 0xff0000) | ((color << 24) & 0xff000000);
      } else {
	color2 = (color & 0xffffff) | ((color << 24) & 0xff000000);
	color1 = (color << 16) | ((color >> 8) & 0xffff);
	color = (color << 8) | ((color >> 16) & 0xff);
      }

      for (; i < lng; i += 4) {
	*(Uint32 *) buf_ptr = color2;
	buf_ptr += 4;
	*(Uint32 *) buf_ptr = color1;
	buf_ptr += 4;
	*(Uint32 *) buf_ptr = color;
	buf_ptr += 4;
      }

      return;
    }
  case 4:
    buf_ptr += (x0 << 2);
    for (i = 0; i < lng; i++, buf_ptr += 4) {
      *(Uint32 *) buf_ptr = color;
    }
    return;
  }
}

/**************************************************************************
  ...
**************************************************************************/
static void put_line(SDL_Surface * pDest, Sint16 x0, Sint16 y0, Sint16 x1,
		     Sint16 y1, Uint32 color)
{
  register int Bpp, pitch;
  register int x, y;
  int dx, dy;
  int sx, sy;
  int swaptmp;
  register Uint8 *pPixel;

  /* correct x0, x1 position ( must be inside 'pDest' surface ) */
  if (x0 < 0) {
    x0 = 0;
  } else if (x0 > pDest->w - 1) {
    x0 = pDest->w - 1;
  }

  if (x1 < 0) {
    x1 = 0;
  } else if (x1 > pDest->w - 1) {
    x1 = pDest->w - 1;
  }

  /* correct y0, y1 position ( must be inside 'pDest' surface ) */
  if (y0 < 0) {
    y0 = 0;
  } else if (y0 > pDest->h - 1) {
    y0 = pDest->h - 1;
  }

  if (y1 < 0) {
    y1 = 0;
  } else if (y1 > pDest->h - 1) {
    y1 = pDest->h - 1;
  }

  /* basic */
  dx = x1 - x0;
  dy = y1 - y0;
  sx = (dx >= 0) ? 1 : -1;
  sy = (dy >= 0) ? 1 : -1;

  /* advanced */
  dx = sx * dx + 1;
  dy = sy * dy + 1;
  Bpp = pDest->format->BytesPerPixel;
  pitch = pDest->pitch;

  pPixel = ((Uint8 *) pDest->pixels + (y0 * pitch) + (x0 * Bpp));

  Bpp *= sx;
  pitch *= sy;

  if (dx < dy) {
    swaptmp = dx;
    dx = dy;
    dy = swaptmp;

    swaptmp = Bpp;
    Bpp = pitch;
    pitch = swaptmp;
  }

  /* draw */
  x = 0;
  y = 0;
  switch (pDest->format->BytesPerPixel) {
  case 1:
    for (; x < dx; x++, pPixel += Bpp) {
      *pPixel = (color & 0xff);
      y += dy;
      if (y >= dx) {
	y -= dx;
	pPixel += pitch;
      }
    }
    return;
  case 2:
    for (; x < dx; x++, pPixel += Bpp) {
      *(Uint16 *) pPixel = (color & 0xffff);
      y += dy;
      if (y >= dx) {
	y -= dx;
	pPixel += pitch;
      }
    }
    return;
  case 3:
    for (; x < dx; x++, pPixel += Bpp) {
      if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
	pPixel[0] = (color >> 16) & 0xff;
	pPixel[1] = (color >> 8) & 0xff;
	pPixel[2] = color & 0xff;
      } else {
	pPixel[0] = (color & 0xff);
	pPixel[1] = (color >> 8) & 0xff;
	pPixel[2] = (color >> 16) & 0xff;
      }

      y += dy;
      if (y >= dx) {
	y -= dx;
	pPixel += pitch;
      }
    }
    return;
  case 4:
    for (; x < dx; x++, pPixel += Bpp) {
      *(Uint32 *) pPixel = color;
      y += dy;
      if (y >= dx) {
	y -= dx;
	pPixel += pitch;
      }
    }
    return;
  }

  return;
}

/**************************************************************************
  Draw line
**************************************************************************/
void putline(SDL_Surface * pDest, Sint16 x0, Sint16 y0,
	     Sint16 x1, Sint16 y1, Uint32 color)
{
  lock_surf(pDest);

  if (!(y1 - y0)) {		/* horizontal line */
    if ((y0 >= 0) && (y0 < pDest->h)) {
      put_hline(pDest, y0, x0, x1, color);
    }
    unlock_surf(pDest);
    return;
  }

  if ((x1 - x0)) {
    put_line(pDest, x0, y0, x1, y1, color);
  } else {			/* vertical line */
    if ((x0 >= 0) && (x0 < pDest->w)) {
      put_vline(pDest, x0, y0, y1, color);
    }
  }

  unlock_surf(pDest);
}

/**************************************************************************
  Draw frame line.
  x0,y0 - top left corrner.
  x1,y1 - botton right corrner.
**************************************************************************/
void putframe(SDL_Surface * pDest, Sint16 x0, Sint16 y0,
	      Sint16 x1, Sint16 y1, Uint32 color)
{
  lock_surf(pDest);

  if ((y0 >= 0) && (y0 < pDest->h)) {	/* top line */
    put_hline(pDest, y0, x0, x1, color);
  }

  if ((y1 >= 0) && (y1 < pDest->h)) {	/* botton line */
    put_hline(pDest, y1, x0, x1 + 1, color);
  }

  if ((x0 >= 0) && (x0 < pDest->w)) {
    put_vline(pDest, x0, y0, y1, color);	/* left line */
  }

  if ((x1 >= 0) && (x1 < pDest->w)) {
    put_vline(pDest, x1, y0, y1, color);	/* right line */
  }

  unlock_surf(pDest);
}

/* ===================================================================== */

/**************************************************************************
  initialize sdl with Flags
**************************************************************************/
void init_sdl(int iFlags)
{

  Main.screen = NULL;

  if (SDL_Init(iFlags) < 0) {
    freelog(LOG_FATAL, _("Plik %s (linia %d): "
			 "Nie moge zainicjalizowaæ biblioteki SDL : %s"),
	    __FILE__, __LINE__, SDL_GetError());
    exit(1);
  }

  atexit(SDL_Quit);

  /* Initialize the TTF library */
  if (TTF_Init() < 0) {
    freelog(LOG_FATAL, _("Plik %s (linia %d): "
			 "Nie moge zainicjalizowaæ biblioteki fontów "
			 "SDL_ttf : %s"),
	    __FILE__, __LINE__, SDL_GetError());
    exit(2);
  }

  atexit(TTF_Quit);
}

/**************************************************************************
  free screen
**************************************************************************/
void quit_sdl(void)
{
  struct RectList *pBuf = Main.rects;
  if (Main.screen) {
    FREESURFACE(Main.screen);
  }
  if (Main.rects_count) {
    Main.rects = NULL;
    Main.rects_count = 0;
    DEL_RECTLIST(pBuf);
  }
}

/**************************************************************************
  Switch to passed video mode.
**************************************************************************/
int set_video_mode(int iWidth, int iHeight, int iFlags)
{
#ifdef DEBUG_SDL
  SDL_PixelFormat *pFmt;
#endif

  /* find best bpp */
  int iDepth = SDL_GetVideoInfo()->vfmt->BitsPerPixel;

  /*  if screen does exist check if this is mayby
     exactly the same resolution then return 1 */
  if (Main.screen) {
    if (Main.screen->w == iWidth && Main.screen->h == iHeight)
      if ((Main.screen->flags & SDL_FULLSCREEN)
	  && (iFlags & SDL_FULLSCREEN))
	return 1;
  }

  /* free old screen */
  if (Main.screen)
    FREESURFACE(Main.screen);

  /* Check to see if a particular video mode is supported */
  if ((iDepth = SDL_VideoModeOK(iWidth, iHeight, iDepth, iFlags)) == 0) {
    freelog(LOG_ERROR, _("Plik %s (linia %d): "
			 "Nie moge znale¼æ ¿adnego dostêpnego trybu dla "
			 "zadanej rozdzielczo¶ci : %d x %d %d bpp"),
	    __FILE__, __LINE__, iWidth, iHeight, iDepth);

    freelog(LOG_DEBUG, _("Plik %s (linia %d): "
			 "Ustawiam domy¶ln± rozdzielczo¶æ : "
			 "640 x 480 16 bpp SW"), __FILE__, __LINE__);

    Main.screen = SDL_SetVideoMode(640, 480, 16, SDL_SWSURFACE);
  } else /* set video mode */
    if ((Main.screen = SDL_SetVideoMode(iWidth, iHeight,
					iDepth, iFlags)) == NULL) {
    freelog(LOG_ERROR, _("Plik %s (linia %d): "
			 "Nie moge ustawiæ rozdzielczo¶æ : "
			 "%d x %d %d bpp %s"),
	    __FILE__, __LINE__, iWidth, iHeight, iDepth, SDL_GetError());
    /* abort program becouse we free old surface. */
    exit(-30);
  }


  freelog(LOG_DEBUG, _("Plik %s (linia %d): \
			Ustawiam rozdzielczo¶æ : %d x %d %d bpp"), __FILE__, __LINE__, iWidth, iHeight, iDepth);

#ifdef DEBUG_SDL
  if (iFlags & SDL_HWSURFACE && !(Main.screen->flags & SDL_HWSURFACE))
    freelog(LOG_DEBUG, _("Plik %s (linia %d): Nie moge stworzyæ \
					ekranu w pamiêci karty... "), __FILE__, __LINE__);
  if (iFlags & SDL_DOUBLEBUF && !(Main.screen->flags & SDL_DOUBLEBUF))
    freelog(LOG_DEBUG, _("Plik %s (linia %d): Nie moge stworzyæ \
			podwójnie buforwanego ekranu ... "), __FILE__, __LINE__);
  if (iFlags & SDL_FULLSCREEN && !(Main.screen->flags & SDL_FULLSCREEN))
    freelog(LOG_DEBUG, _("Plik %s (linia %d): Nie moge prze³±czyæ \
			do trybu pe³noekranowego ... "), __FILE__, __LINE__);

  pFmt = Main.screen->format;
  freelog(LOG_DEBUG, _("Plik %s (linia %d): Format Trybu Video : "
		       "%d x %d Masks: R=%i, G=%i, B=%i | LShft: "
		       "R=%i, G=%i, B=%i |"
		       "RShft: R=%i, G=%i, B=%i BBP: %i ... "),
	  __FILE__, __LINE__, iWidth, iHeight, pFmt->Rmask, pFmt->Gmask,
	  pFmt->Bmask, pFmt->Rshift, pFmt->Gshift, pFmt->Bshift,
	  pFmt->Rloss, pFmt->Gloss, pFmt->Bloss, pFmt->BitsPerPixel);
#endif

  return 0;
}

/**************************************************************************
  Return list of avilabe modes in Unicode.
**************************************************************************/
Uint16 **get_list_modes(Uint32 flags)
{
  int i = 0;
  char __buf[10] = "";
  Uint16 **pUniStringArray = NULL;
  SDL_Rect **pModes = SDL_ListModes(NULL, SDL_FULLSCREEN | flags);

  /* Check is there are any modes available */
  if (pModes == (SDL_Rect **) 0) {
    freelog(LOG_FATAL, _("No modes available!"));
    exit(-1);
  }

  /* Check if or resolution is restricted */
  if (pModes == (SDL_Rect **) - 1) {
    freelog(LOG_DEBUG, _("All resolutions available."));
  } else {
    /* determinate pModes array size */
    while (pModes[i]) {
      i++;
    }

    /* allocate memmory */
    pUniStringArray = CALLOC(i + 1, sizeof(Uint16 *));

    /* fill array */
    for (i = 0; pModes[i]; ++i) {
      sprintf(__buf, "%dx%d", pModes[i]->w, pModes[i]->h);
      pUniStringArray[i] = convert_to_utf16(__buf);
      freelog(LOG_DEBUG, _("Add %s"), __buf);
      /* clear buffor */
      memset(__buf, 0, sizeof(__buf));
    }

  }

  return pUniStringArray;
}

/**************************************************************************
                           Fill Rect with RGBA color
**************************************************************************/
#define MASK565	0xf7de
#define MASK555	0xfbde

/* 50% alpha (128) */
#define BLEND16_50( d, s , mask )	\
	(((( s & mask ) + ( d & mask )) >> 1) + ( s & d & ( ~mask & 0xffff)))

#define BLEND2x16_50( d, s , mask )	\
	(((( s & (mask | mask << 16)) + ( d & ( mask | mask << 16 ))) >> 1) + \
	( s & d & ( ~(mask | mask << 16))))


/**************************************************************************
  ..
**************************************************************************/
static int __FillRectAlpha565(SDL_Surface * pSurface, SDL_Rect * pRect,
			      SDL_Color * pColor)
{
  Uint32 y, end;

  Uint16 *start, *pixel;

  register Uint32 D, S =
      SDL_MapRGB(pSurface->format, pColor->r, pColor->g, pColor->b);
  register Uint32 A = pColor->unused >> 3;

  S &= 0xFFFF;

  if (pRect == NULL) {
    end = pSurface->w * pSurface->h;
    pixel = (Uint16 *) pSurface->pixels;
    if (A == 16) {		/* A == 128 >> 3 */
      if (end & 0x1) {		/* end % 2 */
	D = *pixel;
	*pixel++ = BLEND16_50(D, S, MASK565);
	end--;
      }

      S = S | S << 16;
      for (y = 0; y < end; y += 2) {
	D = *(Uint32 *) pixel;
	*(Uint32 *) pixel = BLEND2x16_50(D, S, MASK565);
	pixel += 2;
      }
    } else {
      S = (S | S << 16) & 0x07e0f81f;
      for (y = 0; y < end; y++) {
	D = *pixel;
	D = (D | D << 16) & 0x07e0f81f;
	D += (S - D) * A >> 5;
	D &= 0x07e0f81f;
	*pixel++ = (D | (D >> 16)) & 0xFFFF;
      }
    }
  } else {
    /* correct pRect size */
    if (pRect->x < 0) {
      pRect->w += pRect->x;
      pRect->x = 0;
    } else {
      if (pRect->x >= pSurface->w - pRect->w) {
	pRect->w = pSurface->w - pRect->x;
      }
    }

    if (pRect->y < 0) {
      pRect->h += pRect->y;
      pRect->y = 0;
    } else {
      if (pRect->y >= pSurface->h - pRect->h) {
	pRect->h = pSurface->h - pRect->y;
      }
    }

    start = pixel = (Uint16 *) pSurface->pixels +
	(pRect->y * (pSurface->pitch >> 1)) + pRect->x;

    if (A == 16) {		/* A == 128 >> 3 */
      S = S | S << 16;
      for (y = 0; y < pRect->h; y++) {
	end = 0;

	if (pRect->w & 0x1) {
	  D = *pixel;
	  *pixel++ = BLEND16_50(D, (S & 0xFFFF), MASK565);
	  end++;
	}

	for (; end < pRect->w; end += 2) {
	  D = *(Uint32 *) pixel;
	  *(Uint32 *) pixel = BLEND2x16_50(D, S, MASK565);
	  pixel += 2;
	}

	pixel = start + (pSurface->pitch >> 1);
	start = pixel;
      }
    } else {
      y = 0;
      S = (S | S << 16) & 0x07e0f81f;
      while (y != pRect->h) {
	D = *pixel;
	D = (D | D << 16) & 0x07e0f81f;
	D += (S - D) * A >> 5;
	D &= 0x07e0f81f;
	*pixel = (D | (D >> 16)) & 0xFFFF;

	if ((pixel - start) == pRect->w) {
	  pixel = start + (pSurface->pitch >> 1);
	  start = pixel;
	  y++;
	} else {
	  pixel++;
	}
      }
    }

  }

  return 0;
}

/**************************************************************************
  ...
**************************************************************************/
static int __FillRectAlpha555(SDL_Surface * pSurface, SDL_Rect * pRect,
			      SDL_Color * pColor)
{
  Uint32 y, end;

  Uint16 *start, *pixel;

  register Uint32 D, S =
      SDL_MapRGB(pSurface->format, pColor->r, pColor->g, pColor->b);
  register Uint32 A = pColor->unused >> 3;

  S &= 0xFFFF;

  if (pRect == NULL) {
    end = pSurface->w * pSurface->h;
    pixel = (Uint16 *) pSurface->pixels;
    if (A == 16) {		/* A == 128 >> 3 */
      if (end & 0x1) {
	D = *pixel;
	*pixel++ = BLEND16_50(D, S, MASK555);
	end--;
      }

      S = S | S << 16;
      for (y = 0; y < end; y += 2) {
	D = *(Uint32 *) pixel;
	*(Uint32 *) pixel = BLEND2x16_50(D, S, MASK555);
	pixel += 2;
      }
    } else {
      S = (S | S << 16) & 0x03e07c1f;
      for (y = 0; y < end; y++) {
	D = *pixel;
	D = (D | D << 16) & 0x03e07c1f;
	D += (S - D) * A >> 5;
	D &= 0x03e07c1f;
	*pixel++ = (D | (D >> 16)) & 0xFFFF;
      }
    }
  } else {
    /* correct pRect size */
    if (pRect->x < 0) {
      pRect->w += pRect->x;
      pRect->x = 0;
    } else {
      if (pRect->x >= pSurface->w - pRect->w) {
	pRect->w = pSurface->w - pRect->x;
      }
    }

    if (pRect->y < 0) {
      pRect->h += pRect->y;
      pRect->y = 0;
    } else {
      if (pRect->y >= pSurface->h - pRect->h) {
	pRect->h = pSurface->h - pRect->y;
      }
    }

    start = pixel = (Uint16 *) pSurface->pixels +
	(pRect->y * (pSurface->pitch >> 1)) + pRect->x;

    if (A == 16) {		/* A == 128 >> 3 */
      S = S | S << 16;
      for (y = 0; y < pRect->h; y++) {
	end = 0;

	if (pRect->w & 0x1) {
	  D = *pixel;
	  *pixel++ = BLEND16_50(D, (S & 0xFFFF), MASK555);
	  end++;
	}

	for (; end < pRect->w; end += 2) {
	  D = *(Uint32 *) pixel;
	  *(Uint32 *) pixel = BLEND2x16_50(D, S, MASK555);
	  pixel += 2;
	}

	pixel = start + (pSurface->pitch >> 1);
	start = pixel;
      }
    } else {
      y = 0;
      S = (S | S << 16) & 0x03e07c1f;
      while (y != pRect->h) {
	D = *pixel;
	D = (D | D << 16) & 0x03e07c1f;
	D += (S - D) * A >> 5;
	D &= 0x03e07c1f;
	*pixel = (D | (D >> 16)) & 0xFFFF;

	if ((pixel - start) == pRect->w) {
	  pixel = start + (pSurface->pitch >> 1);
	  start = pixel;
	  y++;
	} else {
	  pixel++;
	}
      }
    }

  }

  return 0;
}

/**************************************************************************
  ...
**************************************************************************/
static int __FillRectAlpha8888(SDL_Surface * pSurface, SDL_Rect * pRect,
			       SDL_Color * pColor)
{
  Uint32 y, end, A_Dst, A_Mask = pSurface->format->Amask;

  Uint32 *start, *pixel;

  register Uint32 P, D, S1, S2 = SDL_MapRGB(pSurface->format,
					    pColor->r, pColor->g,
					    pColor->b);

  register Uint32 A = pColor->unused;

  S1 = S2 & 0x00FF00FF;


  if (pRect == NULL) {
    end = pSurface->w * pSurface->h;
    pixel = (Uint32 *) pSurface->pixels;
    if (A == 128) {		/* 50% A */
      for (y = 0; y < end; y++) {
	D = *pixel;
	A_Dst = D & A_Mask;
	*pixel++ = ((((S2 & 0x00fefefe) + (D & 0x00fefefe)) >> 1)
		    + (S2 & D & 0x00010101)) | A_Dst;
      }
    } else {
      S2 &= 0xFF00;
      for (y = 0; y < end; y++) {
	D = *pixel;
	A_Dst = D & A_Mask;

	P = D & 0x00FF00FF;
	P += (S1 - P) * A >> 8;
	P &= 0x00ff00ff;

	D = (D & 0xFF00);
	D += (S2 - D) * A >> 8;
	D &= 0xFF00;

	*pixel++ = P | D | A_Dst;
      }
    }

  } else {
    /* correct pRect size */
    if (pRect->x < 0) {
      pRect->w += pRect->x;
      pRect->x = 0;
    } else {
      if (pRect->x >= pSurface->w - pRect->w) {
	pRect->w = pSurface->w - pRect->x;
      }
    }

    if (pRect->y < 0) {
      pRect->h += pRect->y;
      pRect->y = 0;
    } else {
      if (pRect->y >= pSurface->h - pRect->h) {
	pRect->h = pSurface->h - pRect->y;
      }
    }

    start = pixel = (Uint32 *) pSurface->pixels +
	(pRect->y * (pSurface->pitch >> 2)) + pRect->x;

    if (A == 128) {		/* 50% A */

      for (y = 0; y < pRect->h; y++) {

	for (end = 0; end < pRect->w; end++) {
	  D = *pixel;
	  A_Dst = D & A_Mask;
	  *pixel++ = ((((S2 & 0x00fefefe) + (D & 0x00fefefe)) >> 1)
		      + (S2 & D & 0x00010101)) | A_Dst;
	}

	pixel = start + (pSurface->pitch >> 2);
	start = pixel;
      }
    } else {
      y = 0;
      S2 &= 0xFF00;
      while (y != pRect->h) {
	D = *pixel;

	A_Dst = D & A_Mask;

	P = D & 0x00FF00FF;
	P += (S1 - P) * A >> 8;
	P &= 0x00ff00ff;

	D = (D & 0xFF00);
	D += (S2 - D) * A >> 8;
	D &= 0xFF00;

	*pixel = P | D | A_Dst;

	if ((pixel - start) == pRect->w) {
	  pixel = start + (pSurface->pitch >> 2);
	  start = pixel;
	  y++;
	} else {
	  pixel++;
	}
      }
    }
  }

  return 0;
}

/**************************************************************************
  ...
**************************************************************************/
static int __FillRectAlpha888(SDL_Surface * pSurface, SDL_Rect * pRect,
			      SDL_Color * pColor)
{
  Uint32 y, end;

  Uint8 *start, *pixel;

  register Uint32 P, D, S1, S2 = SDL_MapRGB(pSurface->format,
					    pColor->r, pColor->g,
					    pColor->b);

  register Uint32 A = pColor->unused;

  S1 = S2 & 0x00FF00FF;

  S2 &= 0xFF00;

  if (pRect == NULL) {
    end = pSurface->w * pSurface->h;
    pixel = (Uint8 *) pSurface->pixels;

    for (y = 0; y < end; y++) {
      D = *(Uint32 *) pixel;

      P = D & 0x00FF00FF;
      P += (S1 - P) * A >> 8;
      P &= 0x00ff00ff;

      D = (D & 0xFF00);
      D += (S2 - D) * A >> 8;
      D &= 0xFF00;

      P = P | D;

      /* Fix me to little - big EDIAN */

      pixel[0] = P & 0xff;
      pixel[1] = (P >> 8) & 0xff;
      pixel[2] = (P >> 16) & 0xff;

      pixel += 3;
    }

  } else {
    /* correct pRect size */
    if (pRect->x < 0) {
      pRect->w += pRect->x;
      pRect->x = 0;
    } else {
      if (pRect->x >= pSurface->w - pRect->w) {
	pRect->w = pSurface->w - pRect->x;
      }
    }

    if (pRect->y < 0) {
      pRect->h += pRect->y;
      pRect->y = 0;
    } else {
      if (pRect->y >= pSurface->h - pRect->h) {
	pRect->h = pSurface->h - pRect->y;
      }
    }

    end = pRect->w * pRect->h;
    start = pixel = (Uint8 *) pSurface->pixels +
	(pRect->y * pSurface->pitch) + pRect->x * 3;

    y = 0;
    while (y != pRect->h) {
      D = *(Uint32 *) pixel;

      P = D & 0x00FF00FF;
      P += (S1 - P) * A >> 8;
      P &= 0x00ff00ff;

      D = (D & 0xFF00);
      D += (S2 - D) * A >> 8;
      D &= 0xFF00;

      P = P | D;

      /* Fix me to little - big EDIAN */

      pixel[0] = P & 0xff;
      pixel[1] = (P >> 8) & 0xff;
      pixel[2] = (P >> 16) & 0xff;

      if ((pixel - start) == (pRect->w * 3)) {
	pixel = start + pSurface->pitch;
	start = pixel;
	y++;
      } else {
	pixel += 3;
      }

    }

  }

  return 0;
}

/**************************************************************************
  ...
**************************************************************************/
int SDL_FillRectAlpha(SDL_Surface * pSurface, SDL_Rect * pRect,
		      SDL_Color * pColor)
{
  if (pColor->unused == 255) {
    return SDL_FillRect(pSurface, pRect,
			SDL_MapRGB(pSurface->format, pColor->r, pColor->g,
				   pColor->b));
  }

  switch (pSurface->format->BytesPerPixel) {
  case 1:
    /* PORT ME */
    return -1;

  case 2:
    if (pSurface->format->Gmask == 0x7E0) {
      return __FillRectAlpha565(pSurface, pRect, pColor);
    } else {
      if (pSurface->format->Gmask == 0x3E0) {
	return __FillRectAlpha555(pSurface, pRect, pColor);
      } else {
	return -1;
      }
    }
    break;

  case 3:
    return __FillRectAlpha888(pSurface, pRect, pColor);

  case 4:
    return __FillRectAlpha8888(pSurface, pRect, pColor);
  }

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static SDL_Rect *convert_rect_list_to_array(struct RectList *pBuf,
					    int count)
{
  int i = 0;
  SDL_Rect *Array = CALLOC(count, sizeof(SDL_Rect));
  /*struct RectList *pBuf = Main.rects; */

  while (pBuf) {
    Array[i++] = pBuf->area;
    pBuf = pBuf->prev;
  }
  return Array;
}

/**************************************************************************
  ...
**************************************************************************/
static void DEL_RECTLIST(struct RectList *pRectList)
{
  struct RectList *pBuf;

  while (pRectList) {
    pBuf = pRectList->prev;

    FREE(pRectList);

    pRectList = pBuf;
  }
}

/**************************************************************************
  update rectangle (0,0,0,0)->fullscreen
**************************************************************************/
void refresh_screen(Sint16 x, Sint16 y, Uint16 w, Uint16 h)
{
  SDL_Rect dest = { x, y, w, h };

  if (correct_rect_region(&dest))
    SDL_UpdateRect(Main.screen, dest.x, dest.y, dest.w, dest.h);

  return;
}

/**************************************************************************
  ...
**************************************************************************/
void refresh_rect(SDL_Rect Rect)
{

  if (correct_rect_region(&Rect))
    SDL_UpdateRect(Main.screen, Rect.x, Rect.y, Rect.w, Rect.h);

  return;
}

/**************************************************************************
  update fullscreen
**************************************************************************/
void refresh_fullscreen(void)
{
  struct RectList *pBuf = Main.rects;
  int count = Main.rects_count;
  Main.rects = NULL;
  Main.rects_count = 0;
  SDL_UpdateRect(Main.screen, 0, 0, 0, 0);
  if (count) {
    DEL_RECTLIST(pBuf);
  }
}

/**************************************************************************
  draw all update regions
**************************************************************************/
void refresh_rects(void)
{
  struct RectList *pBuf = Main.rects;
  int count = Main.rects_count;

  if (Main.rects_count == 0)
    return;

  Main.rects = NULL;
  Main.rects_count = 0;

  if (count == RECT_LIMIT) {
    SDL_UpdateRect(Main.screen, 0, 0, 0, 0);
  } else {
    SDL_Rect *pRectArray = convert_rect_list_to_array(pBuf, count);
    SDL_UpdateRects(Main.screen, count, pRectArray);
    FREE(pRectArray);
  }

  DEL_RECTLIST(pBuf);
}

/**************************************************************************
  ...
**************************************************************************/
int correct_rect_region(SDL_Rect * pRect)
{
  int ww = pRect->w, hh = pRect->h;

  if (pRect->x < 0) {
    ww += pRect->x;
    pRect->x = 0;
  }

  if (pRect->y < 0) {
    hh += pRect->y;
    pRect->y = 0;
  }

  if (pRect->x + ww > Main.screen->w) {
    ww = Main.screen->w - pRect->x;
  }

  if (pRect->y + hh > Main.screen->h) {
    hh = Main.screen->h - pRect->y;
  }

  /* End Correction */

  if (ww <= 0 || hh <= 0) {
    return 0;			/* suprice :) */
  } else {
    pRect->w = ww;
    pRect->h = hh;
  }

  return 1;
}

/**************************************************************************
  add update region/rect
**************************************************************************/
void add_refresh_rect(SDL_Rect Rect)
{
  struct RectList *pRectL;

  if (correct_rect_region(&Rect)) {
    pRectL = MALLOC(sizeof(struct RectList));
    pRectL->area = Rect;
    pRectL->prev = Main.rects;
    Main.rects = pRectL;
    Main.rects_count++;
  }
}

/**************************************************************************
  add update region/rect
**************************************************************************/
void add_refresh_region(Sint16 x, Sint16 y, Uint16 w, Uint16 h)
{
  struct RectList *pRectL;
  SDL_Rect Rect = { x, y, w, h };

  if (correct_rect_region(&Rect)) {
    pRectL = MALLOC(sizeof(struct RectList));
    pRectL->area = Rect;
    pRectL->prev = Main.rects;
    Main.rects = pRectL;
    Main.rects_count++;
  }
}

/**************************************************************************
  ...
**************************************************************************/
int detect_rect_colisions(SDL_Rect * pMaster, SDL_Rect * pSlave)
{
  int Mx2, My2, Sx2, Sy2;

  if (!pMaster || !pSlave)
    return 0;

  Mx2 = pMaster->x + pMaster->w;
  My2 = pMaster->y + pMaster->h;
  Sx2 = pSlave->x + pSlave->w;
  Sy2 = pSlave->y + pSlave->h;

  /* up left corrner of Slave */
  if ((pMaster->x <= pSlave->x) && (Mx2 > pSlave->x) &&
      (pMaster->y <= pSlave->y) && (My2 > pSlave->y)) {
    return 1;
  }

  /* up right corrner of Slave */
  if ((pMaster->x <= Sx2) && (Mx2 > Sx2) &&
      (pMaster->y <= pSlave->y) && (My2 > pSlave->y)) {
    return 1;
  }

  /* botton left corrner of Slave */
  if ((pMaster->x <= pSlave->x) && (Mx2 > pSlave->x) &&
      (pMaster->y <= Sy2) && (My2 > Sy2)) {
    return 1;
  }

  /* botton right corrner of Slave */
  if ((pMaster->x <= Sx2) && (Mx2 > Sx2) &&
      (pMaster->y <= Sy2) && (My2 > Sy2)) {
    return 1;
  }

  return 0;
}

int cmp_rect(SDL_Rect * pMaster, SDL_Rect * pSlave)
{
  return pMaster->x == pSlave->x
      && pMaster->y == pSlave->y
      && pMaster->w == pSlave->w && pMaster->h == pSlave->h;
}

/* ===================================================================== */

/**************************************************************************
  ...
**************************************************************************/
SDL_Rect get_smaller_surface_rect(SDL_Surface * pSrc)
{
  register int w, h;
  Uint16 minX = pSrc->w, maxX = 0, minY = pSrc->h, maxY = 0;

  SDL_Rect src;

  lock_surf(pSrc);
  for (h = 0; h < pSrc->h; h++) {
    for (w = 0; w < pSrc->w; w++) {
      if (getpixel(pSrc, w, h) != pSrc->format->colorkey) {
	if (minY > h)
	  minY = h;

	if (minX > w)
	  minX = w;

	break;
      }
    }
  }

  for (w = pSrc->w - 1; w >= 0; w--) {
    for (h = pSrc->h - 1; h >= 0; h--) {
      if (getpixel(pSrc, w, h) != pSrc->format->colorkey) {
	if (maxY < h)
	  maxY = h;

	if (maxX < w)
	  maxX = w;

	break;
      }
    }
  }

  unlock_surf(pSrc);
  src.x = minX;
  src.y = minY;
  src.w = maxX - minX + 1;
  src.h = maxY - minY + 1;

  return src;
}

/**************************************************************************
  ...
**************************************************************************/
SDL_Surface *make_flag_surface_smaler(SDL_Surface * pSrc)
{
  SDL_Rect src = get_smaller_surface_rect(pSrc);
  SDL_Surface *pDes = create_surf(src.w, src.h, SDL_SWSURFACE);

  SDL_BlitSurface(pSrc, &src, pDes, NULL);
  SDL_SetColorKey(pDes, SDL_SRCCOLORKEY | SDL_RLEACCEL, 0x0);
  return pDes;
}

/**************************************************************************
  Most black color is coded like {0,0,0,255} but in sdl if alpha is turned 
  off and colorkey is set to 0 this black color is trasparent.
  To fix this we change all black {0, 0, 0, 255} to newblack {4, 4, 4, 255}
  (first collor != 0 in 16 bit coding).
**************************************************************************/
static bool correct_black(SDL_Surface * pSrc)
{
  bool ret = 0;
  if (pSrc->format->BitsPerPixel == 32) {

    register int x;
    register Uint32 alpha, *pPixels = (Uint32 *) pSrc->pixels;

    Uint32 black = SDL_MapRGBA(pSrc->format, 0, 0, 0, 255);
    Uint32 new_black = SDL_MapRGBA(pSrc->format, 4, 4, 4, 255);

    int end = pSrc->w * pSrc->h;

    /* for 32 bit color coding */

    lock_surf(pSrc);

    for (x = 0; x < end; x++, pPixels++) {
      if (*pPixels == black) {
	*pPixels = new_black;
      } else {
	if (!ret) {
	  alpha = *pPixels & 0xff000000;
	  if (alpha && (alpha != 0xff000000))
	    ret = 1;
	}
      }
    }

    unlock_surf(pSrc);
  }

  return ret;
}

#if 0
static int alpha_used(SDL_Surface * pSurf)
{

  if (pSurf->format->BitsPerPixel == 32) {

    register Uint32 *pPixels;
    register Uint32 alpha, i;
    Uint32 End;

    lock_surf(pSurf);

    pPixels = (Uint32 *) pSurf->pixels;

    End = pSurf->h * pSurf->w;

    for (i = 0; i < End; i++, pPixels++) {
      alpha = *pPixels & 0xff000000;
      if (alpha && (alpha != 0xff000000)) {
	unlock_surf(pSurf);
	return 1;
      }
    }

    unlock_surf(pSurf);
  }
  return 0;
}
#endif

/**************************************************************************
  ...
**************************************************************************/
SDL_Surface *get_intro_gfx(void)
{
  return load_surf(pIntro_gfx_path);
}

/**************************************************************************
  ...
**************************************************************************/
SDL_Surface *get_logo_gfx(void)
{
  /*return        load_surf( pLogo_gfx_path ); */
  return IMG_Load(pLogo_gfx_path);
}

/**************************************************************************
  ...
**************************************************************************/
SDL_Surface *get_city_gfx(void)
{
  /*return        load_surf( pLogo_gfx_patch ); */
  return IMG_Load(pCity_gfx_path);
}

/**************************************************************************
  ...
**************************************************************************/
void draw_intro_gfx(void)
{
  SDL_Surface *pIntro = get_intro_gfx();
  /* draw intro gfx center in screen */
  blit_entire_src(pIntro, Main.screen,
		  ((Main.screen->w - pIntro->w) >> 1),
		  ((Main.screen->h - pIntro->h) >> 1));
  FREESURFACE(pIntro);
}

/* ============ FreeCiv game graphics function =========== */

/**************************************************************************
  Return whether the client supports isometric view (isometric tilesets).
**************************************************************************/
bool isometric_view_supported(void)
{
  return TRUE;
}

/**************************************************************************
  Return whether the client supports "overhead" (non-isometric) view.
**************************************************************************/
bool overhead_view_supported(void)
{
  return FALSE;
}

/**************************************************************************
  Load the introductory graphics.
**************************************************************************/
void load_intro_gfx(void)
{
  freelog(LOG_DEBUG, _("intro : %s"),
	  datafilename("theme/default/intro3.png") ?
	  datafilename("theme/default/intro3.png") : "n/a");
  freelog(LOG_DEBUG, _("logo : %s"),
	  datafilename("theme/default/logo.png") ?
	  datafilename("theme/default/logo.png") : "n/a");
  freelog(LOG_DEBUG, _("city : %s"),
	  datafilename("theme/default/city.png") ?
	  datafilename("theme/default/city.png") : "n/a");
  fflush(NULL);

  pIntro_gfx_path = mystrdup(datafilename("theme/default/intro3.png"));
  pLogo_gfx_path = mystrdup(datafilename("theme/default/logo.png"));
  pCity_gfx_path = mystrdup(datafilename("theme/default/city.png"));
}

/**************************************************************************
  Create a new sprite by cropping and taking only the given portion of
  the image.
**************************************************************************/
struct Sprite *crop_sprite(struct Sprite *source,
			   int x, int y, int width, int height)
{
  SDL_Rect src_rect =
      { (Sint16) x, (Sint16) y, (Uint16) width, (Uint16) height };
  SDL_Surface *pNew, *pTmp =
      crop_rect_from_surface((SDL_Surface *) source, &src_rect);

  if (pTmp->format->Amask) {
    SDL_SetAlpha(pTmp, SDL_SRCALPHA, 255);
    pNew = pTmp;
  } else {
    SDL_SetColorKey(pTmp, SDL_SRCCOLORKEY | SDL_RLEACCEL, 0x0);
    pNew = SDL_ConvertSurface(pTmp, pTmp->format, pTmp->flags);

    if (!pNew) {
      return (struct Sprite *) pTmp;
    }

    FREESURFACE(pTmp);
  }

  return (struct Sprite *) pNew;
}

/**************************************************************************
  Load the cursors (mouse substitute sprites), including a goto cursor,
  an airdrop cursor, a nuke cursor, and a patrol cursor.
**************************************************************************/
void load_cursors(void)
{
  /* standart */
  pStd_Cursor = SDL_GetCursor();

  /* goto */
  pGoto_Cursor = init_cursor(goto_cursor_bits, goto_cursor_mask_bits,
			     goto_cursor_width, goto_cursor_height,
			     goto_cursor_x_hot, goto_cursor_y_hot);
  /*2 ,2 ); */

  /* drop */
  pDrop_Cursor = init_cursor(drop_cursor_bits, drop_cursor_mask_bits,
			     drop_cursor_width, drop_cursor_height,
			     drop_cursor_x_hot, drop_cursor_y_hot);

  /* nuke */
  pNuke_Cursor = init_cursor(nuke_cursor_bits, nuke_cursor_mask_bits,
			     nuke_cursor_width, nuke_cursor_height,
			     nuke_cursor_x_hot, nuke_cursor_y_hot);

  /* patrol */
  pPatrol_Cursor = init_cursor(patrol_cursor_bits, patrol_cursor_mask_bits,
			       patrol_cursor_width, patrol_cursor_height,
			       patrol_cursor_x_hot, patrol_cursor_y_hot);
}

/**************************************************************************
  ...
**************************************************************************/
void unload_cursors(void)
{
  SDL_FreeCursor(pStd_Cursor);
  SDL_FreeCursor(pGoto_Cursor);
  SDL_FreeCursor(pDrop_Cursor);
  SDL_FreeCursor(pNuke_Cursor);
  SDL_FreeCursor(pPatrol_Cursor);
  return;
}

/**************************************************************************
  Return a NULL-terminated, permanently allocated array of possible
  graphics types extensions.  Extensions listed first will be checked
  first.
**************************************************************************/
char **gfx_fileextensions(void)
{
  static char *ext[] = {
    "png",
    "xpm",
    NULL
  };

  return ext;
}

/**************************************************************************
  Load the given graphics file into a sprite.  This function loads an
  entire image file, which may later be broken up into individual sprites
  with crop_sprite.
**************************************************************************/
struct Sprite *load_gfxfile(const char *filename)
{
  SDL_Surface *pNew = NULL;
  SDL_Surface *pBuf = NULL;
  bool alpha = 0;

  if ((pBuf = IMG_Load(filename)) == NULL) {
    freelog(LOG_ERROR,
	    _
	    ("load_surf: £adowanie pliku graficznego %s nie powiod³o siê !!"),
	    filename);
    return NULL;		/* Should I use abotr() ? */
  }

  if (pBuf->format->Amask) {
    alpha = correct_black(pBuf);
  }

  if (pBuf->flags & SDL_SRCCOLORKEY) {
    SDL_SetColorKey(pBuf, SDL_SRCCOLORKEY, pBuf->format->colorkey);
  }

  if (alpha) {
    pNew = pBuf;
    freelog(LOG_DEBUG, _("%s load with own format %d!"), filename,
	    pNew->format->BitsPerPixel);
  } else {

    freelog(LOG_DEBUG, _("%s ( %d ) load with screen format %d !"),
	    filename, pBuf->format->BitsPerPixel,
	    Main.screen->format->BitsPerPixel);

    pNew = create_surf(pBuf->w, pBuf->h, SDL_SWSURFACE);

    SDL_SetAlpha(pBuf, 0x0, 0x0);

    if (SDL_BlitSurface(pBuf, NULL, pNew, NULL)) {
      FREESURFACE(pNew);
      return (struct Sprite *) pBuf;
    }

    FREESURFACE(pBuf);

    SDL_SetColorKey(pNew, SDL_SRCCOLORKEY,
		    getpixel(pNew, pNew->w - 1, pNew->h - 1));
  }

  return (struct Sprite *) pNew;
}

/**************************************************************************
  Free a sprite and all associated image data.
**************************************************************************/
void free_sprite(struct Sprite *s)
{
  FREESURFACE((SDL_Surface *) s);
}


/**************************************************************************
  Frees the introductory sprites.
**************************************************************************/
void free_intro_radar_sprites(void)
{
  freelog(LOG_DEBUG, "free_intro_radar_sprites");
}
