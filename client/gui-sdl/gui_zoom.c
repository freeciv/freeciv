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

/***************************************************************************
                          gui_zoom.c  -  description
                             -------------------
    begin                : Jul 25 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 ***************************************************************************/

/***************************************************************************
 *	Based on ...
 *
 *	SDL_rotozoom.c - rotozoomer for 32bit or 8bit surfaces
 *
 *	LGPL (c) A. Schiffler
 *
 ***************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <SDL/SDL.h>

#include "gui_mem.h"
#include "graphics.h"
#include "gui_zoom.h"

/* -O4 -fomit-frame-pointer -fexpensive-optimizations -march=pentium-mmx */
static int AA_ZoomSurfaceFastRGBA32(const SDL_Surface * src,
				    SDL_Surface * dst);
static int AA_ZoomSurfaceFastRGB32(const SDL_Surface * src,
				   SDL_Surface * dst);

static int AA_ZoomSurfaceFastRGB24(const SDL_Surface * src,
				   SDL_Surface * dst);

static int AA_ZoomSurfaceFastRGB16(const SDL_Surface * src,
				   SDL_Surface * dst);
static int AA_ZoomSurfaceFastestRGB16(const SDL_Surface * src,
				      SDL_Surface * dst);

static int AA_ZoomSurfaceFastRGB15(const SDL_Surface * src,
				   SDL_Surface * dst);
static int AA_ZoomSurfaceFastestRGB15(const SDL_Surface * src,
				      SDL_Surface * dst);

static int zoomSurfaceRGB32(const SDL_Surface * src, SDL_Surface * dst);
static int zoomSurfaceRGB24(const SDL_Surface * src, SDL_Surface * dst);
static int zoomSurfaceRGB16(const SDL_Surface * src, SDL_Surface * dst);

static int zoomSurfaceY(const SDL_Surface * src, SDL_Surface * dst);

/**************************************************************************
  used by AA Zoomers
**************************************************************************/
static void precalculate_8bit_row_increments(const SDL_Surface * src,
					     const SDL_Surface * dst,
					     Uint32 * pSax, Uint32 * pSay)
{
  register Uint32 csx, csy;
  Uint32 x, y, sx, sy;
  Uint32 *csax, *csay;

  /*
   * smaller to avoid overflow on right and bottom edge.     
   */
  sx = (Uint32) (255.0 * (float) (src->w - 1) / (float) dst->w);
  sy = (Uint32) (255.0 * (float) (src->h - 1) / (float) dst->h);

  /*
   * Precalculate row increments 
   */
  csx = 0;
  csax = pSax;
  for (x = 0; x <= dst->w; x++) {
    *csax = csx;
    csax++;
    csx &= 0xff;
    csx += sx;
  }

  csy = 0;
  csay = pSay;
  for (y = 0; y <= dst->h; y++) {
    *csay = csy;
    csay++;
    csy &= 0xff;
    csy += sy;
  }
}

/* used by not AA Zoomers */
static void precalculate_16bit_row_increments(const SDL_Surface * src,
					      const SDL_Surface * dst,
					      Uint32 * pSax, Uint32 * pSay)
{
  register Uint32 csx, csy;
  Uint32 x, y, sx, sy;
  Uint32 *csax, *csay;

  sx = (Uint32) (65536.0 * (float) src->w / (float) dst->w);
  sy = (Uint32) (65536.0 * (float) src->h / (float) dst->h);

  /*
   * Precalculate row increments 
   */
  csx = 0;
  csax = pSax;
  for (x = 0; x <= dst->w; x++) {
    *csax = csx;
    csax++;
    csx &= 0xffff;
    csx += sx;
  }

  csy = 0;
  csay = pSay;
  for (y = 0; y <= dst->h; y++) {
    *csay = csy;
    csay++;
    csy &= 0xffff;
    csy += sy;
  }
}

/**************************************************************************
  32bit Zoomer with anti-aliasing by fast bilinear interpolation.

  Zooms 32bit RGBA/ABGR 'src' surface to 'dst' surface.
**************************************************************************/
static int AA_ZoomSurfaceFastRGBA32(const SDL_Surface * src,
				    SDL_Surface * dst)
{
  register Uint32 cc0, cc1, t1, t2, pp, ex, ey;

  Uint32 x, y;
  Uint32 *sax, *say, *csax, *csay;

  Uint32 *c00, *c01, *c10, *c11;
  Uint32 *sp, *csp, *dp;
  int dgap;

  /*
   * Allocate memory for row increments 
   */
  sax = MALLOC((dst->w + 1) * sizeof(Uint32));
  say = MALLOC((dst->h + 1) * sizeof(Uint32));

  precalculate_8bit_row_increments(src, dst, sax, say);


  /*
   * Pointer setup 
   */
  sp = csp = (Uint32 *) src->pixels;
  dp = (Uint32 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 2);

  /*
   * Interpolating Zoom 
   */

  /*
   * Scan destination 
   */
  csay = say;
  for (y = 0; y < dst->h; y++) {
    /*
     * Setup color source pointers 
     */
    c00 = c01 = csp;
    c01++;
    c11 = c10 = (Uint32 *) ((Uint8 *) csp + src->pitch);
    c11++;
    csax = sax;
    for (x = 0; x < dst->w; x++) {

      /*
       * Interpolate colors 
       */
      ex = (*csax & 0xff);
      ey = (*csay & 0xff);

      cc0 = *c00;
      cc0 = (cc0 & 0x00ff00ff);
      cc1 = *c01;
      cc1 = (cc1 & 0x00ff00ff);

      t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      cc0 = *c10;
      cc0 = (cc0 & 0x00ff00ff);
      cc1 = *c11;
      cc1 = (cc1 & 0x00ff00ff);

      t2 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      pp = ((((t2 - t1) * ey) >> 8) + t1) & 0x00ff00ff;

      cc0 = *c00;
      cc0 = (cc0 & 0xff00ff00) >> 8;
      cc1 = *c01;
      cc1 = (cc1 & 0xff00ff00) >> 8;

      t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      cc0 = *c10;
      cc0 = (cc0 & 0xff00ff00) >> 8;
      cc1 = *c11;
      cc1 = (cc1 & 0xff00ff00) >> 8;

      t2 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      pp = pp | ((((((t2 - t1) * ey) >> 8) + t1) & 0x00ff00ff) << 8);

      *dp = pp;

      /*
       * Advance source pointers 
       */
      csax++;
      /* cc0 == step */
      cc0 = (*csax >> 8);
      c00 += cc0;
      c01 += cc0;
      c10 += cc0;
      c11 += cc0;

      /*
       * Advance destination pointer 
       */
      dp++;
    }
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint32 *) ((Uint8 *) csp + (*csay >> 8) * src->pitch);
    /*
     * Advance destination pointers 
     */
    dp = (Uint32 *) ((Uint8 *) dp + dgap);
  }

  /*
   * Remove temp arrays 
   */
  FREE(sax);
  FREE(say);

  return 0;
}

/**************************************************************************
  32bit Zoomer with anti-aliasing by fast bilinear interpolation.

  Zoomes 32bit RGB/BGR 'src' surface to 'dst' surface.

  Amask == 0 and 8-8-8 color coding ( 24bit ) -> no alpha.
**************************************************************************/
static int AA_ZoomSurfaceFastRGB32(const SDL_Surface * src,
				   SDL_Surface * dst)
{
  register Uint32 cc0, cc1, t1, t2, pp, ex, ey;

  Uint32 x, y;
  Uint32 *sax, *say, *csax, *csay;

  Uint32 *c00, *c01, *c10, *c11;
  Uint32 *sp, *csp, *dp;
  int dgap;

  /*
   * Allocate memory for row increments 
   */
  sax = MALLOC((dst->w + 1) * sizeof(Uint32));
  say = MALLOC((dst->h + 1) * sizeof(Uint32));

  precalculate_8bit_row_increments(src, dst, sax, say);


  /*
   * Pointer setup 
   */
  sp = csp = (Uint32 *) src->pixels;
  dp = (Uint32 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 2);

  /*
   * Interpolating Zoom 
   */

  /*
   * Scan destination 
   */
  csay = say;
  for (y = 0; y < dst->h; y++) {
    /*
     * Setup color source pointers 
     */
    c00 = c01 = csp;
    c01++;
    c11 = c10 = (Uint32 *) ((Uint8 *) csp + src->pitch);
    c11++;
    csax = sax;
    for (x = 0; x < dst->w; x++) {

      /*
       * Interpolate colors 
       */
      ex = (*csax & 0xff);
      ey = (*csay & 0xff);

      cc0 = *c00;
      cc0 = (cc0 & 0x00ff00ff);
      cc1 = *c01;
      cc1 = (cc1 & 0x00ff00ff);

      t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      cc0 = *c10;
      cc0 = (cc0 & 0x00ff00ff);
      cc1 = *c11;
      cc1 = (cc1 & 0x00ff00ff);

      t2 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      pp = ((((t2 - t1) * ey) >> 8) + t1) & 0x00ff00ff;

      cc0 = *c00;
      cc1 = *c10;
      cc0 = ((cc1 & 0xff00) << 8) | ((cc0 & 0xff00) >> 8);
      cc1 = *c01;
      t1 = *c11;
      cc1 = ((t1 & 0xff00) << 8) | ((cc1 & 0xff00) >> 8);

      t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      t2 = t1 >> 16;
      t1 &= 0xff;

      pp = pp | ((((((t2 - t1) * ey) >> 8) + t1) & 0xff) << 8);

      *dp = pp | 0xff000000;

      /*
       * Advance source pointers 
       */
      csax++;
      /* cc0 == step */
      cc0 = (*csax >> 8);
      c00 += cc0;
      c01 += cc0;
      c10 += cc0;
      c11 += cc0;

      /*
       * Advance destination pointer 
       */
      dp++;
    }
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint32 *) ((Uint8 *) csp + (*csay >> 8) * src->pitch);
    /*
     * Advance destination pointers 
     */
    dp = (Uint32 *) ((Uint8 *) dp + dgap);
  }

  /*
   * Remove temp arrays 
   */
  FREE(sax);
  FREE(say);

  return 0;
}

/**************************************************************************
  24bit Zoomer with anti-aliasing by fast bilinear interpolation.

  Zooms 24bit RGB/BGR 'src' surface to 'dst' surface.
**************************************************************************/
static int AA_ZoomSurfaceFastRGB24(const SDL_Surface * src,
				   SDL_Surface * dst)
{
  register Uint32 cc0, cc1, t1, t2, pp;

  register Uint32 ex, ey;

  Uint32 x, y;
  Uint32 *sax, *say, *csax, *csay;

  Uint8 *c00, *c01, *c10, *c11;
  Uint8 *sp, *csp, *dp;

  int dgap;

  /*
   * Allocate memory for row increments 
   */
  sax = MALLOC((dst->w + 1) * sizeof(Uint32));
  say = MALLOC((dst->h + 1) * sizeof(Uint32));

  precalculate_8bit_row_increments(src, dst, sax, say);


  /*
   * Pointer setup 
   */
  sp = csp = (Uint8 *) src->pixels;
  dp = (Uint8 *) dst->pixels;
  dgap = dst->pitch - dst->w * 3;

  /*
   * Interpolating Zoom 
   */

  /*
   * Scan destination 
   */
  csay = say;
  for (y = 0; y < dst->h; y++) {
    /*
     * Setup color source pointers 
     */
    c01 = c00 = (Uint8 *) csp;
    c01 += 3;
    c11 = c10 = (Uint8 *) (csp + src->pitch);
    c11 += 3;
    csax = sax;
    for (x = 0; x < dst->w; x++) {

      /*
       * Interpolate colors 
       */
      ex = (*csax & 0xff);
      ey = (*csay & 0xff);

      cc0 = (c00[2] << 16) | c00[0];
      cc1 = (c01[2] << 16) | c01[0];
      t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      cc0 = (c10[2] << 16) | c10[0];
      cc1 = (c11[2] << 16) | c11[0];
      t2 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      pp = ((((t2 - t1) * ey) >> 8) + t1) & 0x00ff00ff;

      cc0 = (c10[1] << 16) | c00[1];
      cc1 = (c11[1] << 16) | c01[1];
      t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      t2 = t1 >> 16;
      t1 &= 0xff;

      pp = pp | ((((((t2 - t1) * ey) >> 8) + t1) & 0xff) << 8);

      dp[0] = pp & 0xff;
      dp[1] = (pp >> 8) & 0xff;
      dp[2] = (pp >> 16) & 0xff;

      /*
       * Advance source pointers 
       */


      csax++;
      /* cc0 == step */
      cc0 = (*csax >> 8);

      /* (cc0 * 3) */
      cc0 = (cc0 << 1) + cc0;

      c00 += cc0;
      c01 += cc0;
      c10 += cc0;
      c11 += cc0;

      /*
       * Advance destination pointer 
       */
      dp += 3;
    }

    /*
     * Advance source pointer 
     */
    csay++;
    csp += (*csay >> 8) * src->pitch;

    /*
     * Advance destination pointers 
     */
    dp += dgap;
  }

  /*
   * Remove temp arrays 
   */
  FREE(sax);
  FREE(say);

  return 0;
}

/**************************************************************************
  16bit Zoomer with anti-aliasing by fast bilinear interpolation.

  Zooms 16bit RGB/BGR 'src' surface to 'dst' surface.
**************************************************************************/
static int AA_ZoomSurfaceFastRGB16(const SDL_Surface * src,
				   SDL_Surface * dst)
{
  register Uint32 cc0, cc1, t1, t2, pp, ex, ey;

  Uint32 x, y;
  Uint32 *sax, *say, *csax, *csay;

  Uint16 *c00, *c01, *c10, *c11;
  Uint16 *sp, *csp, *dp;

  int dgap;

  /*
   * Variable setup 
   */

  /*
   * Allocate memory for row increments 
   */
  sax = MALLOC((dst->w + 1) * sizeof(Uint32));
  say = MALLOC((dst->h + 1) * sizeof(Uint32));

  precalculate_8bit_row_increments(src, dst, sax, say);

  /*
   * Pointer setup 
   */
  sp = csp = (Uint16 *) src->pixels;
  dp = (Uint16 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 1);

  /*
   * Interpolating Zoom 
   */

  /*
   * Scan destination 
   */
  csay = say;
  for (y = 0; y < dst->h; y++) {
    /*
     * Setup color source pointers 
     */
    c00 = c01 = csp;
    c01++;
    c11 = c10 = (Uint16 *) ((Uint8 *) csp + src->pitch);
    c11++;
    csax = sax;
    for (x = 0; x < dst->w; x++) {

      cc0 = *c00;
      if (cc0 != src->format->colorkey) {
	/*
	 * Interpolate colors 
	 */
	ex = (*csax & 0xff);
	ey = (*csay & 0xff);

	/* 16 bit 5-6-5 ( 15bit not supported ) */

	cc0 = /*blue */ (((cc0 >> 11) & 0x1F) << 16) | ((cc0) & 0x1F);	/* red */

	cc1 = *c01;
	cc1 = /*blue */ (((cc1 >> 11) & 0x1F) << 16) | ((cc1) & 0x1F);	/* red */

	t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

	cc0 = *c10;
	cc0 = /*blue */ (((cc0 >> 11) & 0x1F) << 16) | ((cc0) & 0x1F);	/* red */

	cc1 = *c11;
	cc1 = /*blue */ (((cc1 >> 11) & 0x1F) << 16) | ((cc1) & 0x1F);	/* red */

	t2 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

	pp = ((((t2 - t1) * ey) >> 8) + t1) & 0x00ff00ff;

	/* no alpha in 16 bit coding */

	cc0 = *c00;
	t1 = *c10;
	cc0 = (((t1 >> 5) & 0x3F) << 16) | ((cc0 >> 5) & 0x3F);	/* Green */

	cc1 = *c01;
	t1 = *c11;
	cc1 = (((t1 >> 5) & 0x3F) << 16) | ((cc1 >> 5) & 0x3F);	/* Green */

	t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

	t2 = t1 >> 16;
	t1 &= 0xff;

	pp = pp | ((((((t2 - t1) * ey) >> 8) + t1) & 0xff) << 8);

	*dp = (Uint16) (((pp) & 0x1F) |
			((((pp >> 8) & 0x3F)) << 5) |
			((((pp >> 16) & 0x1F)) << 11));

      }

      /*
       * Advance source pointers 
       */
      csax++;
      /* cc0 == step */
      cc0 = (*csax >> 8);
      c00 += cc0;
      c01 += cc0;
      c10 += cc0;
      c11 += cc0;

      /*
       * Advance destination pointer 
       */
      dp++;
    }
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint16 *) ((Uint8 *) csp + (*csay >> 8) * src->pitch);
    /*
     * Advance destination pointers 
     */
    dp = (Uint16 *) ((Uint8 *) dp + dgap);
  }

  /*
   * Remove temp arrays 
   */
  FREE(sax);
  FREE(say);

  return 0;
}

/**************************************************************************
  16bit Zoomer with anti-aliasing by fastest bilinear interpolation.

  Zooms 16bit RGB/BGR 'src' surface to 'dst' surface.
**************************************************************************/
static int AA_ZoomSurfaceFastestRGB16(const SDL_Surface * src,
				      SDL_Surface * dst)
{
  register Uint32 cc0, cc1, t1, t2, ex, ey, pp;

  Uint32 x, y;
  Uint32 *sax, *say, *csax, *csay;

  Uint16 *c00, *c01, *c10, *c11;
  Uint16 *sp, *csp, *dp;

  int dgap;

  /*
   * Variable setup 
   */

  /*
   * Allocate memory for row increments 
   */
  sax = MALLOC((dst->w + 1) * sizeof(Uint32));
  say = MALLOC((dst->h + 1) * sizeof(Uint32));

  precalculate_8bit_row_increments(src, dst, sax, say);

  /*
   * Pointer setup 
   */
  sp = csp = (Uint16 *) src->pixels;
  dp = (Uint16 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 1);

  /*
   * Interpolating Zoom 
   */

  /*
   * Scan destination 
   */
  csay = say;
  for (y = 0; y < dst->h; y++) {
    /*
     * Setup color source pointers 
     */
    c00 = c01 = csp;
    c01++;
    c11 = c10 = (Uint16 *) ((Uint8 *) csp + src->pitch);
    c11++;
    csax = sax;
    for (x = 0; x < dst->w; x++) {

      cc0 = *c00;
      if (cc0 != src->format->colorkey) {

	/*
	 * Interpolate colors 
	 */
	ex = (*csax & 0xff) >> 3;
	ey = (*csay & 0xff) >> 3;

	/* 16 bit 5-6-5 */
	/* move green to upper 16 bit */

	cc0 = (cc0 | cc0 << 16) & 0x07e0f81f;

	cc1 = *c01;
	cc1 = (cc1 | cc1 << 16) & 0x07e0f81f;

	t1 = ((((cc1 - cc0) * ex) >> 5) + cc0) & 0x07e0f81f;

	cc0 = *c10;
	cc0 = (cc0 | cc0 << 16) & 0x07e0f81f;

	cc1 = *c11;
	cc1 = (cc1 | cc1 << 16) & 0x07e0f81f;

	t2 = ((((cc1 - cc0) * ex) >> 5) + cc0) & 0x07e0f81f;


	pp = ((((t2 - t1) * ex) >> 5) + t1) & 0x07e0f81f;

	*dp = (Uint16) (pp | pp >> 16);

      }

      /*
       * Advance source pointers 
       */
      csax++;
      /* cc0 == step */
      cc0 = (*csax >> 8);
      c00 += cc0;
      c01 += cc0;
      c10 += cc0;
      c11 += cc0;

      /*
       * Advance destination pointer 
       */
      dp++;
    }
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint16 *) ((Uint8 *) csp + (*csay >> 8) * src->pitch);
    /*
     * Advance destination pointers 
     */
    dp = (Uint16 *) ((Uint8 *) dp + dgap);
  }

  /*
   * Remove temp arrays 
   */
  FREE(sax);
  FREE(say);

  return 0;
}

/**************************************************************************
  15bit Zoomer with anti-aliasing by fast bilinear interpolation.

  Zooms 15bit RGBA/ABGR 'src' surface to 'dst' surface.
**************************************************************************/
static int AA_ZoomSurfaceFastRGB15(const SDL_Surface * src,
				   SDL_Surface * dst)
{
  register Uint32 cc0, cc1, t1, t2, pp, ex, ey;

  Uint32 x, y;
  Uint32 *sax, *say, *csax, *csay;

  Uint16 *c00, *c01, *c10, *c11;
  Uint16 *sp, *csp, *dp;

  int dgap;

  /*
   * Variable setup 
   */

  /*
   * Allocate memory for row increments 
   */
  sax = MALLOC((dst->w + 1) * sizeof(Uint32));
  say = MALLOC((dst->h + 1) * sizeof(Uint32));

  precalculate_8bit_row_increments(src, dst, sax, say);

  /*
   * Pointer setup 
   */
  sp = csp = (Uint16 *) src->pixels;
  dp = (Uint16 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 1);

  /*
   * Interpolating Zoom 
   */

  /*
   * Scan destination 
   */
  csay = say;
  for (y = 0; y < dst->h; y++) {
    /*
     * Setup color source pointers 
     */
    c00 = c01 = csp;
    c01++;
    c11 = c10 = (Uint16 *) ((Uint8 *) csp + src->pitch);
    c11++;
    csax = sax;
    for (x = 0; x < dst->w; x++) {

      cc0 = *c00;
      if (cc0 != src->format->colorkey) {
	/*
	 * Interpolate colors 
	 */
	ex = *csax & 0xff;
	ey = *csay & 0xff;

	/* 15 bit 5-5-5  */

	cc0 = /*blue */ (((cc0 >> 10) & 0x1F) << 16) | ((cc0) & 0x1F);	/* red */

	cc1 = *c01;
	cc1 = /*blue */ (((cc1 >> 10) & 0x1F) << 16) | ((cc1) & 0x1F);	/* red */

	t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

	cc0 = *c10;
	cc0 = /*blue */ (((cc0 >> 10) & 0x1F) << 16) | ((cc0) & 0x1F);	/* red */

	cc1 = *c11;
	cc1 = /*blue */ (((cc1 >> 10) & 0x1F) << 16) | ((cc1) & 0x1F);	/* red */

	t2 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

	pp = ((((t2 - t1) * ey) >> 8) + t1) & 0x00ff00ff;

	/* no alpha in 15 bit coding */

	cc0 = *c00;
	t1 = *c10;
	cc0 = (((t1 >> 5) & 0x1F) << 16) | ((cc0 >> 5) & 0x1F);	/* Green */

	cc1 = *c01;
	t1 = *c11;
	cc1 = (((t1 >> 5) & 0x1F) << 16) | ((cc1 >> 5) & 0x1F);	/* Green */

	t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

	t2 = t1 >> 16;
	t1 &= 0xff;

	pp = pp | ((((((t2 - t1) * ey) >> 8) + t1) & 0xff) << 8);

	*dp = (Uint16) (((pp) & 0x1F) | ((((pp >> 8) & 0x1F)) << 5) |
			((((pp >> 16) & 0x1F)) << 11));

      }

      /*
       * Advance source pointers 
       */
      csax++;
      /* cc0 == step */
      cc0 = (*csax >> 8);
      c00 += cc0;
      c01 += cc0;
      c10 += cc0;
      c11 += cc0;

      /*
       * Advance destination pointer 
       */
      dp++;
    }
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint16 *) ((Uint8 *) csp + (*csay >> 8) * src->pitch);
    /*
     * Advance destination pointers 
     */
    dp = (Uint16 *) ((Uint8 *) dp + dgap);
  }

  /*
   * Remove temp arrays 
   */
  FREE(sax);
  FREE(say);

  return 0;
}

/**************************************************************************
  15bit Zoomer with anti-aliasing by fastest bilinear interpolation.

  Zooms 15bit RGB/BGR 'src' surface to 'dst' surface.
**************************************************************************/
static int AA_ZoomSurfaceFastestRGB15(const SDL_Surface * src,
				      SDL_Surface * dst)
{
  register Uint32 cc0, cc1, t1, t2, ex, ey, pp;

  Uint32 x, y;
  Uint32 *sax, *say, *csax, *csay;

  Uint16 *c00, *c01, *c10, *c11;
  Uint16 *sp, *csp, *dp;

  int dgap;

  /*
   * Variable setup 
   */

  /*
   * Allocate memory for row increments 
   */
  sax = MALLOC((dst->w + 1) * sizeof(Uint32));
  say = MALLOC((dst->h + 1) * sizeof(Uint32));

  precalculate_8bit_row_increments(src, dst, sax, say);

  /*
   * Pointer setup 
   */
  sp = csp = (Uint16 *) src->pixels;
  dp = (Uint16 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 1);

  /*
   * Interpolating Zoom 
   */

  /*
   * Scan destination 
   */
  csay = say;
  for (y = 0; y < dst->h; y++) {
    /*
     * Setup color source pointers 
     */
    c00 = c01 = csp;
    c01++;
    c11 = c10 = (Uint16 *) ((Uint8 *) csp + src->pitch);
    c11++;
    csax = sax;
    for (x = 0; x < dst->w; x++) {

      cc0 = *c00;
      if (cc0 != src->format->colorkey) {

	/*
	 * Interpolate colors 
	 */
	ex = (*csax & 0xff) >> 3;
	ey = (*csay & 0xff) >> 3;

	/* 15 bit 5-5-5 */

	cc0 = (cc0 | cc0 << 16) & 0x03e07c1f;

	cc1 = *c01;
	cc1 = (cc1 | cc1 << 16) & 0x03e07c1f;

	t1 = ((((cc1 - cc0) * ex) >> 5) + cc0) & 0x03e07c1f;

	cc0 = *c10;
	cc0 = (cc0 | cc0 << 16) & 0x03e07c1f;

	cc1 = *c11;
	cc1 = (cc1 | cc1 << 16) & 0x03e07c1f;

	t2 = ((((cc1 - cc0) * ex) >> 5) + cc0) & 0x03e07c1f;


	pp = ((((t2 - t1) * ex) >> 5) + t1) & 0x03e07c1f;

	*dp = (Uint16) (pp | pp >> 16);

      }

      /*
       * Advance source pointers 
       */
      csax++;
      /* cc0 == step */
      cc0 = (*csax >> 8);
      c00 += cc0;
      c01 += cc0;
      c10 += cc0;
      c11 += cc0;

      /*
       * Advance destination pointer 
       */
      dp++;
    }
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint16 *) ((Uint8 *) csp + (*csay >> 8) * src->pitch);
    /*
     * Advance destination pointers 
     */
    dp = (Uint16 *) ((Uint8 *) dp + dgap);
  }

  /*
   * Remove temp arrays 
   */
  FREE(sax);
  FREE(say);

  return 0;
}

/**************************************************************************
  32bit Zoomer.

  Zoomes 32bit RGB/BGR 'src' surface to 'dst' surface.
**************************************************************************/
static int zoomSurfaceRGB32(const SDL_Surface * src, SDL_Surface * dst)
{
  Uint32 x, y;
  Uint32 *sax, *say, *csax, *csay;

  Uint32 *sp, *csp, *dp;
  int dgap;

  /*
   * Allocate memory for row increments 
   */
  sax = MALLOC((dst->w + 1) * sizeof(Uint32));
  say = MALLOC((dst->h + 1) * sizeof(Uint32));

  precalculate_16bit_row_increments(src, dst, sax, say);

  /*
   * Pointer setup 
   */
  sp = csp = (Uint32 *) src->pixels;
  dp = (Uint32 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 2);

  /*
   * Non-Interpolating Zoom 
   */

  csay = say;
  for (y = 0; y < dst->h; y++) {
    sp = csp;
    csax = sax;
    for (x = 0; x < dst->w; x++) {
      /*
       * Draw 
       */
      *dp = *sp;
      /*
       * Advance source pointers 
       */
      csax++;
      sp += (*csax >> 16);
      /*
       * Advance destination pointer 
       */
      dp++;
    }
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint32 *) ((Uint8 *) csp + (*csay >> 16) * src->pitch);
    /*
     * Advance destination pointers 
     */
    dp = (Uint32 *) ((Uint8 *) dp + dgap);
  }

  /*
   * Remove temp arrays 
   */
  FREE(sax);
  FREE(say);

  return 0;
}

/**************************************************************************
  24bit Zoomer.

  Zoomes 24bit RGBA/ABGR 'src' surface to 'dst' surface. 
**************************************************************************/
static int zoomSurfaceRGB24(const SDL_Surface * src, SDL_Surface * dst)
{
  Uint32 x, y;
  register Uint32 step;
  Uint32 *sax, *say, *csax, *csay;

  Uint8 *sp, *csp, *dp;
  int dgap;

  /*
   * Allocate memory for row increments 
   */
  sax = MALLOC((dst->w + 1) * sizeof(Uint32));
  say = MALLOC((dst->h + 1) * sizeof(Uint32));

  precalculate_16bit_row_increments(src, dst, sax, say);

  /*
   * Pointer setup 
   */
  sp = csp = (Uint8 *) src->pixels;
  dp = (Uint8 *) dst->pixels;
  dgap = dst->pitch - dst->w * 3;

  /*
   * Non-Interpolating Zoom 
   */

  csay = say;
  for (y = 0; y < dst->h; y++) {
    sp = csp;
    csax = sax;
    for (x = 0; x < dst->w; x++) {
      /*
       * Draw 
       */
      dp[0] = sp[0];
      dp[1] = sp[1];
      dp[2] = sp[2];

      /*
       * Advance source pointers 
       */
      csax++;
      step = (*csax >> 16);
      step = (step << 1) + step;	/* (... * 3) */
      sp += step;

      /*
       * Advance destination pointer 
       */
      dp += 3;
    }
    /*
     * Advance source pointer 
     */
    csay++;
    csp += ((*csay >> 16) * src->pitch);
    /*
     * Advance destination pointers 
     */
    dp += dgap;
  }

  /*
   * Remove temp arrays 
   */
  FREE(sax);
  FREE(say);

  return 0;
}

/**************************************************************************
  15/16bit Zoomer.

  Zoomes 16bit RGBA/ABGR 'src' surface to 'dst' surface.
**************************************************************************/
static int zoomSurfaceRGB16(const SDL_Surface * src, SDL_Surface * dst)
{
  Uint32 x, y;
  Uint32 *sax, *say, *csax, *csay;
  Uint16 *sp, *csp, *dp;
  int dgap;

  /*
   * Allocate memory for row increments 
   */
  sax = MALLOC((dst->w + 1) * sizeof(Uint32));
  say = MALLOC((dst->h + 1) * sizeof(Uint32));

  precalculate_16bit_row_increments(src, dst, sax, say);

  /*
   * Pointer setup 
   */
  sp = csp = (Uint16 *) src->pixels;
  dp = (Uint16 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 1);

  /*
   * Non-Interpolating Zoom 
   */

  csay = say;
  for (y = 0; y < dst->h; y++) {
    sp = csp;
    csax = sax;
    for (x = 0; x < dst->w; x++) {
      /*
       * Draw 
       */
      *dp = *sp;
      /*
       * Advance source pointers 
       */
      csax++;
      sp += (*csax >> 16);
      /*
       * Advance destination pointer 
       */
      dp++;
    }
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint16 *) ((Uint8 *) csp + (*csay >> 16) * src->pitch);
    /*
     * Advance destination pointers 
     */
    dp = (Uint16 *) ((Uint8 *) dp + dgap);
  }

  /*
   * Remove temp arrays 
   */
  FREE(sax);
  FREE(say);

  return 0;
}

/**************************************************************************
  8bit Zoomer without smoothing.

  Zoomes 8bit palette/Y 'src' surface to 'dst' surface.
**************************************************************************/
static int zoomSurfaceY(const SDL_Surface * src, SDL_Surface * dst)
{
  Uint32 x, y, sx, sy, *sax, *say, *csax, *csay;
  register Uint32 csx, csy;
  Uint8 *sp, *dp, *csp;
  int dgap;

  /*
   * Variable setup 
   */
  sx = (Uint32) (65536.0 * (float) src->w / (float) dst->w);
  sy = (Uint32) (65536.0 * (float) src->h / (float) dst->h);

  /*
   * Allocate memory for row increments 
   */
  if ((sax = (Uint32 *) malloc(dst->w * sizeof(Uint32))) == NULL) {
    return (-1);
  }

  if ((say = (Uint32 *) malloc(dst->h * sizeof(Uint32))) == NULL) {
    if (sax != NULL) {
      free(sax);
    }
    return -1;
  }

  /*
   * Precalculate row increments 
   */
  csx = 0;
  csax = sax;
  for (x = 0; x < dst->w; x++) {
    csx += sx;
    *csax = (csx >> 16);
    csx &= 0xffff;
    csax++;
  }

  csy = 0;
  csay = say;
  for (y = 0; y < dst->h; y++) {
    csy += sy;
    *csay = (csy >> 16);
    csy &= 0xffff;
    csay++;
  }

  csx = 0;
  csax = sax;
  for (x = 0; x < dst->w; x++) {
    csx += *csax;
    csax++;
  }

  csy = 0;
  csay = say;
  for (y = 0; y < dst->h; y++) {
    csy += *csay;
    csay++;
  }

  /*
   * Pointer setup 
   */
  sp = csp = (Uint8 *) src->pixels;
  dp = (Uint8 *) dst->pixels;
  dgap = dst->pitch - dst->w;

  /*
   * Draw 
   */
  csay = say;
  for (y = 0; y < dst->h; y++) {
    csax = sax;
    sp = csp;
    for (x = 0; x < dst->w; x++) {
      /*
       * Draw 
       */
      *dp = *sp;
      /*
       * Advance source pointers 
       */
      sp += *csax;
      csax++;
      /*
       * Advance destination pointer 
       */
      dp++;
    }

    /*
     * Advance source pointer (for row) 
     */
    csp += (*csay * src->pitch);
    csay++;
    /*
     * Advance destination pointers 
     */
    dp += dgap;
  }

  /*
   * Remove temp arrays 
   */
  free(sax);
  free(say);

  return 0;
}

#define VALUE_LIMIT	0.001

/**************************************************************************
  zoomSurfaceSize( ... )
  Calculate new width and height from the scaling factors
  ('zoomx' and 'zoomy') and size of 'src' surface
  ( 'width' and 'height').
**************************************************************************/
static void ZoomSurfaceSize(Uint16 width, Uint16 height, double zoomx,
			    double zoomy, int *pNew_width,
			    int *pNew_height)
{
  /*
   * Sanity check zoom factors 
   */
  zoomx = MAX(zoomx, VALUE_LIMIT);
  zoomy = MAX(zoomy, VALUE_LIMIT);

  /*
   * Calculate target size 
   */
  *pNew_width = (int) ((double) width * zoomx);
  *pNew_height = (int) ((double) height * zoomy);
  *pNew_width = MAX(*pNew_width, 1);
  *pNew_height = MAX(*pNew_height, 1);
}

/* ============================================================== */
/* ======================= Public Functions ===================== */
/* ============================================================== */

/**************************************************************************
  ZoomSurface(...)

  Zoomes a 32bit, 24bit, 16bit or 8bit 'src' surface to newly created
  'dst' surface. 'zoomx' and 'zoomy' are scaling factors for width and
  height.  If 'smooth' is 1 or 2 then the destination surface is
  anti-aliased.

  'smooth' = ..
    0 = normal zoom.
    1 = zoom with AA.
    2 = zoom with Faster AA ( lower quality ) <-> only 16bit surfaces.

  ZoomSurface calls ResizeSurface(...) to do main job.
**************************************************************************/
SDL_Surface *ZoomSurface(const SDL_Surface * pSrc, double zoomx,
			 double zoomy, int smooth)
{
  int dstwidth, dstheight;
  /* Get size if target */
  ZoomSurfaceSize(pSrc->w, pSrc->h, zoomx, zoomy, &dstwidth, &dstheight);
  return ResizeSurface(pSrc, (Uint16) dstwidth, (Uint16) dstheight,
		       smooth);
}

/* 
*/
/**************************************************************************
  ResizeSurface(...)

  Zoomes a 32bit, 24bit, 16bit or 8bit 'src' surface to newly created
  'dst' surface. The 'new_width' and 'new_height' are new size 
  of 'dst' surface.
  If 'smooth' is 1 or 2 then the destination surface is anti-aliased.

  'smooth' = ..
    0 = normal resize.
    1 = resize with AA.
    2 = resize with Faster AA ( lower quality ) <-> only 16bit surfaces.
**************************************************************************/
SDL_Surface *ResizeSurface(const SDL_Surface * pSrc, Uint16 new_width,
			   Uint16 new_height, int smooth)
{
  SDL_Surface *pReal_dst = NULL, *pBuf_Src = (SDL_Surface *) pSrc;
  int i;

  /*
   * Sanity check 
   */
  if (pSrc == NULL) {
    return NULL;
  }

  /*
   * Alloc space to completely contain the zoomed surface 
   */
  pReal_dst = SDL_CreateRGBSurface(SDL_SWSURFACE,
				   new_width, new_height,
				   pSrc->format->BitsPerPixel,
				   pSrc->format->Rmask,
				   pSrc->format->Gmask,
				   pSrc->format->Bmask,
				   pSrc->format->Amask);

  /*
   * Lock source surface 
   */
  lock_surf(pBuf_Src);

  /*
   * Check which kind of surface we have 
   */
  switch (pSrc->format->BitsPerPixel) {
  case 32:
    if (smooth) {
      /*
       *      Call the 32bit transformation routine to do
       *      the zooming (using alpha) with faster AA algoritm
       */
      if (pReal_dst->format->Amask) {
	/* RGBA -> 8-8-8-8 -> 32 bit */
	AA_ZoomSurfaceFastRGBA32(pSrc, pReal_dst);
      } else {			/* no alpha -> 24 bit coding 8-8-8 ) */
	AA_ZoomSurfaceFastRGB32(pSrc, pReal_dst);
      }
      /*
       * Turn on source-alpha support 
       */
      SDL_SetAlpha(pReal_dst, SDL_SRCALPHA, 255);

    } else {

      /*
       *      Call the 32bit transformation routine
       *      to do the zooming
       */
      zoomSurfaceRGB32(pSrc, pReal_dst);

    }
    break;
  case 24:
    if (smooth) {
      /*
       *      Call the 24bit transformation routine to do
       *      the zooming (using alpha) with faster AA algoritm
       */
      AA_ZoomSurfaceFastRGB24(pSrc, pReal_dst);
      /*
       * Turn on source-alpha support 
       */
      SDL_SetAlpha(pReal_dst, SDL_SRCALPHA, 255);
    } else {
      /*
       *      Call the 24bit transformation routine
       *      to do the zooming
       */
      zoomSurfaceRGB24(pSrc, pReal_dst);
    }
    break;
  case 16:
    switch (smooth) {
    case 1:
      if (pReal_dst->format->Gmask == 0x7E0) {
	/*
	 *      Call the 16bit (5-6-5) transformation routine to do
	 *      the zooming (using alpha) with faster AA algoritm
	 */
	AA_ZoomSurfaceFastRGB16(pSrc, pReal_dst);
      } else if (pReal_dst->format->Gmask == 0x3E0) {
	/*
	 *      Call the 15bit (5-5-5) transformation routine to do
	 *      the zooming (using alpha) with faster AA algoritm
	 */
	AA_ZoomSurfaceFastRGB15(pSrc, pReal_dst);
      } else {
	FREESURFACE(pReal_dst);
	return NULL;
      }
      /*
       * Turn on source-alpha support 
       */
      SDL_SetAlpha(pReal_dst, SDL_SRCALPHA, 255);

      break;
    case 2:
      if (pReal_dst->format->Gmask == 0x7E0) {
	/*
	 *      Call the 16bit (5-6-5) transformation routine to do
	 *      the zooming (using alpha) with faster AA algoritm
	 *      ( lower quality ).      
	 */
	AA_ZoomSurfaceFastestRGB16(pSrc, pReal_dst);
      } else if (pReal_dst->format->Gmask == 0x3E0) {
	/*
	 *    Call the 15bit (5-5-5) transformation routine to do
	 *    the zooming (using alpha) with faster AA algoritm
	 *    ( lower quality ). 
	 */
	AA_ZoomSurfaceFastestRGB15(pSrc, pReal_dst);
      } else {
	FREESURFACE(pReal_dst);
	return NULL;
      }

      /*
       * Turn on source-alpha support 
       */
      SDL_SetAlpha(pReal_dst, SDL_SRCALPHA, 255);

      break;
    default:

      /*
       *      Call the 16bit transformation routine
       *      to do the zooming
       */
      zoomSurfaceRGB16(pSrc, pReal_dst);

      break;
    }
    break;
  case 8:

    /*
     * Copy palette and colorkey info 
     */
    for (i = 0; i < pSrc->format->palette->ncolors; i++) {
      pReal_dst->format->palette->colors[i] =
	  pSrc->format->palette->colors[i];
    }
    pReal_dst->format->palette->ncolors = pSrc->format->palette->ncolors;

    /*
     * Call the 8bit transformation routine to do the zooming 
     */
    zoomSurfaceY(pSrc, pReal_dst);
    SDL_SetColorKey(pReal_dst, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		    pSrc->format->colorkey);

    break;
  default:
    goto END;
    break;
  }

END:
  /*
   * Unlock source surface 
   */
  unlock_surf(pBuf_Src);


  /*
   * Return destination surface 
   */
  return pReal_dst;
}
