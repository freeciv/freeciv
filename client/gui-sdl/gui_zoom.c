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
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 ***************************************************************************/
/***************************************************************************
 *	Based on ...
 *
 *	SDL_rotozoom.c - rotozoomer for 32bit or 8bit surfaces
 *
 *	LGPL (c) A. Schiffler
 *
 ***************************************************************************/
/***************************************************************************
 *	Bilinear Interpolation.
 *      
 *	Maybe BI don't get best qulity in zoom but is faster that I know.
 * 	
 *      Basicaly it blend 4 src pixels ...
 *       
 *	p[n](P0)         p[n + 1](P1)
 *	p[n + pitch](p2) p[n + pitch + 1](P3)
 *	
 *	P0 blend P1 -> t1
 *	                  \
 *			   blend -> dst pixel
 *			  /
 *	P2 blend P3 -> t2
 *
 *  For Blend I use basic alpha blend code without A = 255 check.
 *  d = (((s-d)*(A))>>8)+d; - s and d are colors in Src and Dst pixels
 ***************************************************************************/
/***************************************************************************
 *	TODO:
 *		- add cpu mmx reg. runtime detection code.
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <SDL/SDL.h>

#include "gui_mem.h"
#include "graphics.h"
#include "gui_zoom.h"

#ifdef HAVE_MMX1
#include "mmx.h"
#endif

static int AA_ZoomSurfaceRGBn(const SDL_Surface *src, SDL_Surface *dst);

static int AA_ZoomSurfaceFastRGBA32(const SDL_Surface * src,
				    SDL_Surface * dst);

static int AA_ZoomSurfaceFastRGB24(const SDL_Surface * src,
				   SDL_Surface * dst);

static int AA_ZoomSurfaceFastRGB16(const SDL_Surface * src,
				   SDL_Surface * dst);
static int AA_ZoomSurfaceFastRGB15(const SDL_Surface * src,
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

#ifdef HAVE_MMX1

/**************************************************************************
  32bit Zoomer with anti-aliasing by fast bilinear interpolation.

  Zooms 32bit RGBA/ABGR 'src' surface to 'dst' surface using MMX1 code.
**************************************************************************/
static int AA_ZoomSurfaceFastRGBA32(const SDL_Surface *src, SDL_Surface *dst)
{
  Uint32 width, height;
  Uint32 *sax, *say, *csax, *csay;
  register Uint32 *sp, *spp, *csp, *dp;
  int dgap, step;
    
  /*
   * Allocate memory for row increments 
   */
  if ((sax = (Uint32 *) MALLOC((dst->w + 1) * sizeof(Uint32))) == NULL) {
		return (-1);
  }
  if ((say = (Uint32 *) MALLOC((dst->h + 1) * sizeof(Uint32))) == NULL) {
    FREE(sax);
    return (-1);
  }

  precalculate_8bit_row_increments( src, dst, sax , say );
    
  emms();
  dgap = 0x00ff00ff;
  movd_m2r(*(&dgap), mm7); /* mask -> mmm7 */
  punpckldq_r2r(mm7, mm7); /* full mask (0x00ff00ff00ff00ff) -> mm7 */
        
  /*
   * Pointer setup 
   */
  sp = csp = (Uint32 *) src->pixels;
  spp = (Uint32 *) ((Uint8 *) csp + src->pitch);
  dp = (Uint32 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 2);

  /*
   * Interpolating Zoom 
   */

  csay = say;
  csax = sax;
  height = dst->h;
  width = dst->w;
  while(height--) {
    DUFFS_LOOP4 (
    {
      movd_m2r(*csax, mm6); /* ex -> mm6 */
      pand_r2r(mm7, mm6); /* mm6 & Emask -> mm6(0000000E) */
      punpcklwd_r2r(mm6, mm6); /* 00000E0E -> mm6 */
      punpckldq_r2r(mm6, mm6); /* 0E0E0E0E -> mm6 */
      
      movd_m2r(*csay, mm5); /* ey -> mm5 */
      pand_r2r(mm7, mm5); /* mm5 & Emask -> mm6(0000000Y) */
      punpcklwd_r2r(mm5, mm5); /* 00000Y0Y -> mm5 */
      punpckldq_r2r(mm5, mm5); /* 0Y0Y0Y0Y -> mm5 */
			
      movq_m2r(*sp, mm0); /* sp0sp1 -> mm0(ARGBARGB) */
      movq_r2r(mm0, mm1); /* sp0sp1 -> mm1(ARGBARGB) */
      punpcklbw_r2r(mm0, mm0); /* low - AARRGGBB -> mm0 */
      punpckhbw_r2r(mm1, mm1); /* high - AARRGGBB -> mm1 */
      pand_r2r(mm7, mm0); /* mask & mm0 -> mm0(0A0R0G0B) sp0 */
      pand_r2r(mm7, mm1); /* mask & mm1 -> mm1(0A0R0G0B) sp1 */
	      
      movq_m2r(*spp, mm2); /* spp0spp1 -> mm2(ARGBARGB) */
      movq_r2r(mm2, mm3); /* spp0spp1 -> mm3(ARGBARGB) */
      punpcklbw_r2r(mm2, mm2); /* low - AARRGGBB -> mm2 */
      punpckhbw_r2r(mm3, mm3); /* high - AARRGGBB -> mm3 */
      pand_r2r(mm7, mm2); /* mask & mm2 -> mm2(0A0R0G0B) spp0 */
      pand_r2r(mm7, mm3); /* mask & mm3 -> mm3(0A0R0G0B) spp1 */
	      
      /* blend (sp0 vs sp1) -> t1 and (spp0 vs spp1) -> t2 */     
      psubw_r2r(mm0, mm1);/* sp1 - sp0 -> mm1 */
      psubw_r2r(mm2, mm3);/* spp1 - spp0 -> mm3 */
      pmullw_r2r(mm6, mm1); /* mm1 * ex -> mm1 */
      pmullw_r2r(mm6, mm3); /* mm3 * ex -> mm3 */
      psrlw_i2r(8, mm1); /* mm1 >> 8 -> mm1 */
      psrlw_i2r(8, mm3); /* mm3 >> 8 -> mm3 */
      paddw_r2r(mm1, mm0); /* mm1 + mm0(sp0) -> mm0 */
      paddw_r2r(mm3, mm2); /* mm3 + mm2(spp0) -> mm2 */
      pand_r2r(mm7, mm0); /* mask & mm0 -> mm0(0A0R0G0B) t1 */
      pand_r2r(mm7, mm2); /* mask & mm2 -> mm2(0A0R0G0B) t2 */
			
      /* blend t1 vs t2 */
      psubw_r2r(mm0, mm2);/* t2 - t1 -> mm2 */
      pmullw_r2r(mm5, mm2); /* mm2 * ey -> mm2 */
      psrlw_i2r(8, mm2); /* mm2 >> 8 -> mm2 */
      paddw_r2r(mm2, mm0); /* mm2 + mm0(t1) -> mm0 */
      pand_r2r(mm7, mm0); /* mask & mm0 -> mm0(0A0R0G0B) result */
      packuswb_r2r(mm0, mm0);  /* ARGBARGB -> mm2 */
      movd_r2m(mm0, *dp);/* mm0 -> dst pixel */
	    
      /*
       * Advance source pointers 
       */
      csax++;
      step = *csax;
      step >>= 8;
      sp += step;
      spp += step;
      /*
       * Advance destination pointer 
       */
       dp++;
    }, width);
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint32 *) ((Uint8 *) csp + (*csay >> 8) * src->pitch);
    sp = csp;
    spp = (Uint32 *) ((Uint8 *) csp + src->pitch);
    csax = sax;
    /*
     * Advance destination pointers 
     */
    dp = (Uint32 *) ((Uint8 *) dp + dgap);
  }
  emms();    

  /*
   * Remove temp arrays 
   */
  FREE(sax);
  FREE(say);

  return (0);
}

/**************************************************************************
  24bit Zoomer with anti-aliasing by fast bilinear interpolation.

  Zooms 24bit RGB/BGR 'src' surface to 'dst' surface using MMX1 code.
**************************************************************************/
static int AA_ZoomSurfaceFastRGB24(const SDL_Surface *src, SDL_Surface *dst)
{
  Uint32 width, height, step;
  Uint32 *sax, *say, *csax, *csay;
  register Uint8 *sp, *spp, *csp, *dp;
  int dgap;
    
  /*
   * Allocate memory for row increments 
   */
  if ((sax = (Uint32 *) MALLOC((dst->w + 1) * sizeof(Uint32))) == NULL) {
    return (-1);
  }
  if ((say = (Uint32 *) MALLOC((dst->h + 1) * sizeof(Uint32))) == NULL) {
    FREE(sax);
    return (-1);
  }

  precalculate_8bit_row_increments( src, dst, sax , say );
    
  emms();
  dgap = 0x00ff00ff;
  movd_m2r(*(&dgap), mm7); /* mask -> mmm7 */
  punpckldq_r2r(mm7, mm7); /* full mask (0x00ff00ff00ff00ff) -> mm7 */
            
  /*
   * Pointer setup 
   */
  sp = csp = (Uint8 *) src->pixels;
  spp = (Uint8 *) csp + src->pitch;
  dp = (Uint8 *) dst->pixels;
  dgap = dst->pitch - dst->w * 3;

  /*
   * Interpolating Zoom 
   */

  csay = say;
  csax = sax;
  height = dst->h;
  width = dst->w;
  while(height--) {
    DUFFS_LOOP4 (
    {
      movd_m2r(*csax, mm6); /* ex -> mm6 */
      pand_r2r(mm7, mm6); /* mm6 & Emask -> mm6(0000000E) */
      punpcklwd_r2r(mm6, mm6); /* 00000E0E -> mm6 */
      punpckldq_r2r(mm6, mm6); /* 0E0E0E0E -> mm6 */

      movd_m2r(*csay, mm5); /* ey -> mm5 */
      pand_r2r(mm7, mm5); /* mm5 & Emask -> mm6(0000000Y) */
      punpcklwd_r2r(mm5, mm5); /* 00000Y0Y -> mm5 */
      punpckldq_r2r(mm5, mm5); /* 0Y0Y0Y0Y -> mm5 */
			
      movq_m2r(*sp, mm0); /* sp0sp1 -> mm0(GBRGBRGB) */
      movq_r2r(mm0, mm1); /* sp0sp1 -> mm1(GBRGBRGB) */
      psllq_i2r(8, mm1); /* mm1 << 8 -> mm1(BRGBRGB0) */
      punpcklbw_r2r(mm0, mm0); /* low - bbRRGGBB -> mm0 */
      punpckhbw_r2r(mm1, mm1); /* high - bbRRGGBB -> mm1 */
      pand_r2r(mm7, mm0); /* mask & mm0 -> mm0(0b0R0G0B) sp0 */
      pand_r2r(mm7, mm1); /* mask & mm1 -> mm1(0b0R0G0B) sp1 */
	      
      movq_m2r(*spp, mm2); /* spp0spp1 -> mm2(GBRGBRGB) */
      movq_r2r(mm2, mm3); /* spp0spp1 -> mm3(GBRGBRGB) */
      psllq_i2r(8, mm3); /* mm1 << 8 -> mm1(BRGBRGB0) */
      punpcklbw_r2r(mm2, mm2); /* low - bbRRGGBB -> mm2 */
      punpckhbw_r2r(mm3, mm3); /* high - bbRRGGBB -> mm3 */
      pand_r2r(mm7, mm2); /* mask & mm2 -> mm2(0b0R0G0B) spp0 */
      pand_r2r(mm7, mm3); /* mask & mm3 -> mm3(0b0R0G0B) spp1 */
	      
      /* blend (sp0 vs sp1) -> t1 and (spp0 vs spp1) -> t2 */     
      psubw_r2r(mm0, mm1);/* sp1 - sp0 -> mm1 */
      psubw_r2r(mm2, mm3);/* spp1 - spp0 -> mm3 */
      pmullw_r2r(mm6, mm1); /* mm1 * ex -> mm1 */
      pmullw_r2r(mm6, mm3); /* mm3 * ex -> mm3 */
      psrlw_i2r(8, mm1); /* mm1 >> 8 -> mm1 */
      psrlw_i2r(8, mm3); /* mm3 >> 8 -> mm3 */
      paddw_r2r(mm1, mm0); /* mm1 + mm0(sp0) -> mm0 */
      paddw_r2r(mm3, mm2); /* mm3 + mm2(spp0) -> mm2 */
      pand_r2r(mm7, mm0); /* mask & mm0 -> mm0(0A0R0G0B) t1 */
      pand_r2r(mm7, mm2); /* mask & mm2 -> mm2(0A0R0G0B) t2 */
			
      /* blend t1 vs t2 */
      psubw_r2r(mm0, mm2);/* t2 - t1 -> mm2 */
      pmullw_r2r(mm5, mm2); /* mm2 * ey -> mm2 */
      psrlw_i2r(8, mm2); /* mm2 >> 8 -> mm2 */
      paddw_r2r(mm2, mm0); /* mm2 + mm0(t1) -> mm0 */
      pand_r2r(mm7, mm0); /* mask & mm0 -> mm0(0b0R0G0B) result */
      packuswb_r2r(mm0, mm0);  /* bRGBbRGB -> mm0 */
	    
      if(height != 1) {
        /* WARNING this may be dangerous with last pixel becouse it
           write 4 byes ( not 3 ) to "dp". Normaly it is save becouse
           we overwrite this 4-th bye by next write */
        movd_r2m(mm0, *dp);/* mm0 -> dst pixel */
      } else {
	/* for sure last line copy in normal way */
        movd_r2m(mm0, *(&step));/* mm0 -> dst pixel -> step */
        dp[0] = step & 0xff;
        dp[1] = (step >> 8 ) & 0xff;
        dp[2] = (step >> 16) & 0xff;
      }
      
      /*
       * Advance source pointers 
       */
      csax++;
      step = *csax;
      step >>= 8;
      step = (step << 1) + step;	/* step * 3 */
      sp += step;
      spp += step;
      /*
       * Advance destination pointer 
       */
      dp+=3;
    }, width);
    /*
     * Advance source pointer 
     */
    csay++;
    csp += ((*csay >> 8) * src->pitch);
    sp = csp;
    spp = (Uint8 *)(csp + src->pitch);
    csax = sax;
    /*
     * Advance destination pointers 
     */
    dp += dgap;
  }
  emms();    

  /*
   * Remove temp arrays 
   */
  FREE(sax);
  FREE(say);

  return (0);
}

/**************************************************************************
  16bit Zoomer with anti-aliasing by fast bilinear interpolation.

  Zooms 16bit RGB/BGR 'src' surface to 'dst' surface using MMX1 code.
**************************************************************************/
static int AA_ZoomSurfaceFastRGB16(const SDL_Surface *src, SDL_Surface *dst)
{
  register Uint16 *sp, *spp, *csp, *dp;
  Uint32 *sax, *say, *csax, *csay;
  Uint32 width, height;
  int dgap, step;
    
  /*
   * Allocate memory for row increments 
   */
  if ((sax = (Uint32 *) MALLOC((dst->w + 1) * sizeof(Uint32))) == NULL) {
    return (-1);
  }
  if ((say = (Uint32 *) MALLOC((dst->h + 1) * sizeof(Uint32))) == NULL) {
    FREE(sax);
    return (-1);
  }

  precalculate_8bit_row_increments( src, dst, sax , say );
    
  emms();
  dgap = 0x00ff00ff;
  movd_m2r(*(&dgap), mm7); /* mask -> mmm7 */
  punpckldq_r2r(mm7, mm7); /* full mask (0x00ff00ff00ff00ff) -> mm7 */
    
  dgap = 0xf800;/* red mask */
  movd_m2r(*(&dgap), mm4); /* red mask -> mmm4 */
  dgap = 0x07e0;/* green mask */
  movd_m2r(*(&dgap), mm5); /* gree mask -> mmm5 */
  dgap = 0x001f;/* blue mask */
  movd_m2r(*(&dgap), mm6); /* blue mask -> mmm7 */
        
  /*
   * Pointer setup 
   */
  sp = csp = (Uint16 *) src->pixels;
  spp = (Uint16 *) ((Uint8 *) csp + src->pitch);
  dp = (Uint16 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 1);

  /*
   * Interpolating Zoom 
   */
  csay = say;
  csax = sax;
  height = dst->h;
  width = dst->w;
  while(height--) {
    DUFFS_LOOP4 (
    {
      /* load sp0 */
      movd_m2r(*sp, mm2); /* sp0sp1 -> mm2(0000CLCL) */
      movq_r2r(mm2, mm0); /* sp0sp1 -> mm0(0000CLCL) */
      movq_r2r(mm2, mm1); /* sp0sp1 -> mm1(0000CLCL) */
      pand_r2r(mm6, mm0); /* blue mask & mm0 -> mm0 (0000000B) */
      pand_r2r(mm5, mm1); /* green mask & mm1 -> mm1 */
      pslld_i2r(11, mm1); /* mm1 << 11 -> mm1 (00000G00)*/
      por_r2r(mm1, mm0); /* mm1 | mm0 -> mm0 (00000G0B)*/
      movq_r2r(mm2, mm1); /* sp0sp1 -> mm1(0000CLCL) */
      pand_r2r(mm4, mm1); /* red mask & mm1 -> mm1 */
      psllq_i2r(21, mm1); /* mm1 << 21 -> mm1 (000R0000)*/
      por_r2r(mm1, mm0); /* mm1 | mm0 -> mm0 (000R0G0B) sp0 */
	    
      /* load sp1 */
      psrld_i2r(16, mm2); /* mm2 >> 16 -> mm2 (000000CL)*/
      movq_r2r(mm2, mm1); /* sp1 -> mm1(000000CL) */
      pand_r2r(mm6, mm1); /* blue mask & mm1 -> mm1 (0000000B) */
      psrld_i2r(5, mm2); /* mm2 >> 5 -> mm2 clear blue */
      psllq_i2r(26, mm2); /* mm2 << 16 -> mm2 */
      por_r2r(mm2, mm1); /* mm2 | mm1 -> mm1 set red */
      psrlq_i2r(21, mm2); /* mm2 >> 21 -> mm2 */
      pand_r2r(mm5, mm2); /* mask & mm1 -> mm1 clear red */
      pslld_i2r(11, mm2); /* mm1 << 11 -> mm1 (00000G00)*/
      por_r2r(mm2, mm1); /* mm2 | mm1 -> mm1 set green */
      pand_r2r(mm7, mm1); /* mask & mm1 -> mm1 (000R0G0B) sp1 */
	                
      /* blend (sp0 vs sp1) -> t1 */
      movd_m2r(*csax, mm3); /* ex -> mm6 */
      pand_r2r(mm7, mm3); /* mm6 & Emask -> mm6(0000000E) */
      punpcklwd_r2r(mm3, mm3); /* 00000E0E -> mm6 */
      punpckldq_r2r(mm3, mm3); /* 0E0E0E0E -> mm6 */
	    
      psubw_r2r(mm0, mm1);/* sp1 - sp0 -> mm1 */
      pmullw_r2r(mm3, mm1); /* mm1 * ex -> mm1 */
      psrlw_i2r(8, mm1); /* mm1 >> 8 -> mm1 */
      paddw_r2r(mm1, mm0); /* mm1 + mm0(sp0) -> mm0 */
      pand_r2r(mm7, mm0); /* mask & mm0 -> mm0(0A0R0G0B) t1 */
    
      /* load spp0 */
      movd_m2r(*spp, mm1); /* spp0spp1 -> mm1(0000C2C1) */
      movq_r2r(mm1, mm2); /* spp0spp1 -> mm2(0000C2C1) */
      movq_r2r(mm1, mm3); /* spp0spp1 -> mm3(0000C2C1) */
      pand_r2r(mm6, mm2); /* blue mask & mm2 -> mm2 (0000000B) */
      pand_r2r(mm5, mm3); /* green mask & mm3 -> mm3 */
      pslld_i2r(11, mm3); /* mm3 << 11 -> mm3 (00000G00)*/
      por_r2r(mm3, mm2); /* mm3 | mm2 -> mm2 (00000G0B)*/
      movq_r2r(mm1, mm3); /* spp0spp1 -> mm3(0000C2C1) */
      pand_r2r(mm4, mm3); /* red mask & mm3 -> mm3 */
      psllq_i2r(21, mm3); /* mm3 << 21 -> mm3 (000R0000)*/
      por_r2r(mm3, mm2); /* mm3 | mm2 -> mm0 (000R0G0B) spp0 */
    
      /* load spp1 */	    
      psrld_i2r(16, mm1); /* mm1 >> 16 -> mm1 (000000CL)*/
      movq_r2r(mm1, mm3); /* spp1 -> mm3(000000CL) */
      pand_r2r(mm6, mm3); /* blue mask & mm3 -> mm3 (0000000B) */
      psrld_i2r(5, mm1); /* mm1 >> 5 -> mm1 clear blue */
      psllq_i2r(26, mm1); /* mm1 << 26 -> mm1 */
      por_r2r(mm1, mm3); /* mm1 | mm3 -> mm3 set red */
      psrlq_i2r(21, mm1); /* mm1 >> 21 -> mm1 */
      pand_r2r(mm5, mm1); /* green mask & mm1 -> mm1 */
      pslld_i2r(11, mm1); /* mm1 << 11 -> mm1 (00000G00)*/
      por_r2r(mm1, mm3); /* mm2 | mm3 -> mm3 set green */
      pand_r2r(mm7, mm3); /* mask & mm3 -> mm3 (000R0G0B) spp1 */
	      
      /* blend  (spp0 vs spp1) -> t2 */
      movd_m2r(*csax, mm1); /* ex -> mm1 */
      pand_r2r(mm7, mm1); /* mm1 & Emask -> mm6(0000000E) */
      punpcklwd_r2r(mm1, mm1); /* 00000E0E -> mm1 */
      punpckldq_r2r(mm1, mm1); /* 0E0E0E0E -> mm1 */
	    
      psubw_r2r(mm2, mm3);/* spp1 - spp0 -> mm3 */
      pmullw_r2r(mm1, mm3); /* mm3 * ex -> mm3 */
      psrlw_i2r(8, mm3); /* mm3 >> 8 -> mm3 */
      paddw_r2r(mm3, mm2); /* mm3 + mm2(spp0) -> mm2 */
      pand_r2r(mm7, mm2); /* mask & mm2 -> mm2(0A0R0G0B) t2 */
			
      /* blend t1 vs t2 */
      movd_m2r(*csay, mm3); /* ey -> mm5 */
      pand_r2r(mm7, mm3); /* mm5 & Emask -> mm6(0000000Y) */
      punpcklwd_r2r(mm3, mm3); /* 00000Y0Y -> mm5 */
      punpckldq_r2r(mm3, mm3); /* 0Y0Y0Y0Y -> mm5 */
	    
      psubw_r2r(mm0, mm2);/* t2 - t1 -> mm2 */
      pmullw_r2r(mm3, mm2); /* mm2 * ey -> mm2 */
      psrlw_i2r(8, mm2); /* mm2 >> 8 -> mm2 */
      paddw_r2r(mm2, mm0); /* mm2 + mm0(t1) -> mm0 */
      pand_r2r(mm7, mm0); /* mask & mm0 -> mm0(0A0R0G0B) result */
	    
      packuswb_r2r(mm0, mm0);  /* ARGBARGB -> mm2 */
      movd_r2m(mm0, *(&step));/* mm0 -> dst pixel */

      /* convert RGB888 to RGB565 pixel coding */
      *dp = (Uint16)( ((step) & 0x1F) | ( (((step >> 8) & 0x3F)) << 5) |
					( (((step >> 16) & 0x1F)) << 11));
	    
      /*
       * Advance source pointers 
       */
      csax++;
      step = *csax;
      step >>= 8;
      sp += step;
      spp += step;
      /*
       * Advance destination pointer 
       */
      dp++;
    }, width);
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint16 *) ((Uint8 *) csp + (*csay >> 8) * src->pitch);
    sp = csp;
    spp = (Uint16 *) ((Uint8 *) csp + src->pitch);
    csax = sax;
    /*
     * Advance destination pointers 
     */
    dp = (Uint16 *) ((Uint8 *) dp + dgap);
  }
  emms();    

  /*
   * Remove temp arrays 
   */
  FREE(sax);
  FREE(say);
  
  return (0);
}

/**************************************************************************
  15bit Zoomer with anti-aliasing by fast bilinear interpolation.

  Zooms 15bit RGBA/ABGR 'src' surface to 'dst' surface using MMX1 code.
**************************************************************************/
static int AA_ZoomSurfaceFastRGB15(const SDL_Surface *src, SDL_Surface *dst)
{
  register Uint16 *sp, *spp, *csp, *dp;
  Uint32 *sax, *say, *csax, *csay;
  Uint32 width, height;
  int dgap, step;
    
  /*
   * Allocate memory for row increments 
   */
  if ((sax = (Uint32 *) MALLOC((dst->w + 1) * sizeof(Uint32))) == NULL) {
    return (-1);
  }
  if ((say = (Uint32 *) MALLOC((dst->h + 1) * sizeof(Uint32))) == NULL) {
    FREE(sax);
    return (-1);
  }

  precalculate_8bit_row_increments( src, dst, sax , say );
    
  emms();
  dgap = 0x00ff00ff;
  movd_m2r(*(&dgap), mm7); /* mask -> mmm7 */
  punpckldq_r2r(mm7, mm7); /* full mask (0x00ff00ff00ff00ff) -> mm7 */
    
  dgap = 0x7c00;/* red mask */
  movd_m2r(*(&dgap), mm4); /* red mask -> mmm4 */
  dgap = 0x03e0;/* green mask */
  movd_m2r(*(&dgap), mm5); /* gree mask -> mmm5 */
  dgap = 0x001f;/* blue mask */
  movd_m2r(*(&dgap), mm6); /* blue mask -> mmm7 */
        
  /*
   * Pointer setup 
   */
  sp = csp = (Uint16 *) src->pixels;
  spp = (Uint16 *) ((Uint8 *) csp + src->pitch);
  dp = (Uint16 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 1);

  /*
   * Interpolating Zoom 
   */
  csay = say;
  csax = sax;
  height = dst->h;
  width = dst->w;
  while(height--) {
    DUFFS_LOOP4 (
    {
      /* load sp0 */
      movd_m2r(*sp, mm2); /* sp0sp1 -> mm2(0000CLCL) */
      movq_r2r(mm2, mm0); /* sp0sp1 -> mm0(0000CLCL) */
      movq_r2r(mm2, mm1); /* sp0sp1 -> mm1(0000CLCL) */
      pand_r2r(mm6, mm0); /* blue mask & mm0 -> mm0 (0000000B) */
      pand_r2r(mm5, mm1); /* green mask & mm1 -> mm1 */
      pslld_i2r(11, mm1); /* mm1 << 11 -> mm1 (00000G00)*/
      por_r2r(mm1, mm0); /* mm1 | mm0 -> mm0 (00000G0B)*/
      movq_r2r(mm2, mm1); /* sp0sp1 -> mm1(0000CLCL) */
      pand_r2r(mm4, mm1); /* red mask & mm1 -> mm1 */
      psllq_i2r(22, mm1); /* mm1 << 22 -> mm1 (000R0000)*/
      por_r2r(mm1, mm0); /* mm1 | mm0 -> mm0 (000R0G0B) sp0 */
	    
      /* load sp1 */
      psrld_i2r(16, mm2); /* mm2 >> 16 -> mm2 (000000CL)*/
      movq_r2r(mm2, mm1); /* sp1 -> mm1(000000CL) */
      pand_r2r(mm6, mm1); /* blue mask & mm1 -> mm1 (0000000B) */
      psrld_i2r(5, mm2); /* mm2 >> 5 -> mm2 clear blue */
      psllq_i2r(27, mm2); /* mm2 << 27 -> mm2 */
      por_r2r(mm2, mm1); /* mm2 | mm1 -> mm1 set red */
      psrlq_i2r(22, mm2); /* mm2 >> 22 -> mm2 */
      pand_r2r(mm5, mm2); /* mask & mm1 -> mm1 clear red */
      pslld_i2r(11, mm2); /* mm1 << 11 -> mm1 (00000G00)*/
      por_r2r(mm2, mm1); /* mm2 | mm1 -> mm1 set green */
      pand_r2r(mm7, mm1); /* mask & mm1 -> mm1 (000R0G0B) sp1 */
	                
      /* blend (sp0 vs sp1) -> t1 */
      movd_m2r(*csax, mm3); /* ex -> mm6 */
      pand_r2r(mm7, mm3); /* mm6 & Emask -> mm6(0000000E) */
      punpcklwd_r2r(mm3, mm3); /* 00000E0E -> mm6 */
      punpckldq_r2r(mm3, mm3); /* 0E0E0E0E -> mm6 */
	    
      psubw_r2r(mm0, mm1);/* sp1 - sp0 -> mm1 */
      pmullw_r2r(mm3, mm1); /* mm1 * ex -> mm1 */
      psrlw_i2r(8, mm1); /* mm1 >> 8 -> mm1 */
      paddw_r2r(mm1, mm0); /* mm1 + mm0(sp0) -> mm0 */
      pand_r2r(mm7, mm0); /* mask & mm0 -> mm0(0A0R0G0B) t1 */
    
      /* load spp0 */
      movd_m2r(*spp, mm1); /* spp0spp1 -> mm1(0000C2C1) */
      movq_r2r(mm1, mm2); /* spp0spp1 -> mm2(0000C2C1) */
      movq_r2r(mm1, mm3); /* spp0spp1 -> mm3(0000C2C1) */
      pand_r2r(mm6, mm2); /* blue mask & mm2 -> mm2 (0000000B) */
      pand_r2r(mm5, mm3); /* green mask & mm3 -> mm3 */
      pslld_i2r(11, mm3); /* mm3 << 11 -> mm3 (00000G00)*/
      por_r2r(mm3, mm2); /* mm3 | mm2 -> mm2 (00000G0B)*/
      movq_r2r(mm1, mm3); /* spp0spp1 -> mm3(0000C2C1) */
      pand_r2r(mm4, mm3); /* red mask & mm3 -> mm3 */
      psllq_i2r(22, mm3); /* mm3 << 21 -> mm3 (000R0000)*/
      por_r2r(mm3, mm2); /* mm3 | mm2 -> mm0 (000R0G0B) spp0 */
    
      /* load spp1 */	    
      psrld_i2r(16, mm1); /* mm1 >> 16 -> mm1 (000000CL)*/
      movq_r2r(mm1, mm3); /* spp1 -> mm3(000000CL) */
      pand_r2r(mm6, mm3); /* blue mask & mm3 -> mm3 (0000000B) */
      psrld_i2r(5, mm1); /* mm1 >> 5 -> mm1 clear blue */
      psllq_i2r(27, mm1); /* mm1 << 26 -> mm1 */
      por_r2r(mm1, mm3); /* mm1 | mm3 -> mm3 set red */
      psrlq_i2r(22, mm1); /* mm1 >> 21 -> mm1 */
      pand_r2r(mm5, mm1); /* green mask & mm1 -> mm1 */
      pslld_i2r(11, mm1); /* mm1 << 11 -> mm1 (00000G00)*/
      por_r2r(mm1, mm3); /* mm2 | mm3 -> mm3 set green */
      pand_r2r(mm7, mm3); /* mask & mm3 -> mm3 (000R0G0B) spp1 */
	      
      /* blend  (spp0 vs spp1) -> t2 */
      movd_m2r(*csax, mm1); /* ex -> mm1 */
      pand_r2r(mm7, mm1); /* mm1 & Emask -> mm6(0000000E) */
      punpcklwd_r2r(mm1, mm1); /* 00000E0E -> mm1 */
      punpckldq_r2r(mm1, mm1); /* 0E0E0E0E -> mm1 */
	    
      psubw_r2r(mm2, mm3);/* spp1 - spp0 -> mm3 */
      pmullw_r2r(mm1, mm3); /* mm3 * ex -> mm3 */
      psrlw_i2r(8, mm3); /* mm3 >> 8 -> mm3 */
      paddw_r2r(mm3, mm2); /* mm3 + mm2(spp0) -> mm2 */
      pand_r2r(mm7, mm2); /* mask & mm2 -> mm2(0A0R0G0B) t2 */
			
      /* blend t1 vs t2 */
      movd_m2r(*csay, mm3); /* ey -> mm5 */
      pand_r2r(mm7, mm3); /* mm5 & Emask -> mm6(0000000Y) */
      punpcklwd_r2r(mm3, mm3); /* 00000Y0Y -> mm5 */
      punpckldq_r2r(mm3, mm3); /* 0Y0Y0Y0Y -> mm5 */
	    
      psubw_r2r(mm0, mm2);/* t2 - t1 -> mm2 */
      pmullw_r2r(mm3, mm2); /* mm2 * ey -> mm2 */
      psrlw_i2r(8, mm2); /* mm2 >> 8 -> mm2 */
      paddw_r2r(mm2, mm0); /* mm2 + mm0(t1) -> mm0 */
      pand_r2r(mm7, mm0); /* mask & mm0 -> mm0(0A0R0G0B) result */
	    
      packuswb_r2r(mm0, mm0);  /* ARGBARGB -> mm2 */
      movd_r2m(mm0, *(&step));/* mm0 -> dst pixel */

      /* convert RGB888 to RGB555 pixel coding */
      *dp = (Uint16)( ((step) & 0x1F) | ( (((step >> 8) & 0x1F)) << 5) |
					( (((step >> 16) & 0x1F)) << 10));
	    
      /*
       * Advance source pointers 
       */
      csax++;
      step = *csax;
      step >>= 8;
      sp += step;
      spp += step;
      /*
       * Advance destination pointer 
       */
      dp++;
    }, width);
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint16 *) ((Uint8 *) csp + (*csay >> 8) * src->pitch);
    sp = csp;
    spp = (Uint16 *) ((Uint8 *) csp + src->pitch);
    csax = sax;
    /*
     * Advance destination pointers 
     */
    dp = (Uint16 *) ((Uint8 *) dp + dgap);
  }
  emms();    

  /*
   * Remove temp arrays 
   */
  FREE(sax);
  FREE(say);
  
  return (0);
}

#else

static int AA_ZoomSurfaceFastRGB32(const SDL_Surface *src, SDL_Surface *dst);
static int AA_ZoomSurfaceFastestRGB15(const SDL_Surface * src,
				      	SDL_Surface * dst);
static int AA_ZoomSurfaceFastestRGB16(const SDL_Surface * src,
				      	SDL_Surface * dst);

/**************************************************************************
  32bit Zoomer with anti-aliasing by fast bilinear interpolation.

  Zooms 32bit Full RGBA/ABGR 'src' surface to 'dst' surface.
**************************************************************************/
static int AA_ZoomSurfaceFastRGBA32(const SDL_Surface * src,
				    SDL_Surface * dst)
{
  Uint32 width, height;
  Uint32 *sax, *say, *csax, *csay;
  register Uint32 cc0, cc1, t1, t2, pp, ex, ey;
  Uint32 *sp0 , *sp1, *spp0 , *spp1, *csp, *dp;
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
  sp0 = sp1 = csp = (Uint32 *) src->pixels;
  sp1++;
  spp0 = spp1 = (Uint32 *) ((Uint8 *) csp + src->pitch);
  spp1++;
  dp = (Uint32 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 2);

  /*
   * Interpolating Zoom 
   */

  /*
   * Scan destination 
   */
  csay = say;
  csax = sax;
  height = dst->h;
  width = dst->w;
  while(height--) {
    DUFFS_LOOP4 (
    {

      /*
       * Interpolate colors 
       */
      ex = (*csax & 0xff);
      ey = (*csay & 0xff);

      cc0 = *sp0;
      cc0 = (cc0 & 0x00ff00ff);
      cc1 = *sp1;
      cc1 = (cc1 & 0x00ff00ff);

      t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      cc0 = *spp0;
      cc0 = (cc0 & 0x00ff00ff);
      cc1 = *spp1;
      cc1 = (cc1 & 0x00ff00ff);

      t2 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      pp = ((((t2 - t1) * ey) >> 8) + t1) & 0x00ff00ff;

      cc0 = *sp0;
      cc0 = (cc0 & 0xff00ff00) >> 8;
      cc1 = *sp1;
      cc1 = (cc1 & 0xff00ff00) >> 8;

      t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      cc0 = *spp0;
      cc0 = (cc0 & 0xff00ff00) >> 8;
      cc1 = *spp1;
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
      sp0 += cc0;
      sp1 += cc0;
      spp0 += cc0;
      spp1 += cc0;

      /*
       * Advance destination pointer 
       */
      dp++;
    }, width);
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint32 *) ((Uint8 *) csp + (*csay >> 8) * src->pitch);
    sp0 = sp1 = csp;
    sp1++;
    spp0 = spp1 = (Uint32 *) ((Uint8 *) csp + src->pitch);
    spp1++;
    csax = sax;
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
  Uint32 width, height;
  Uint32 *sax, *say, *csax, *csay;
  register Uint32 cc0, cc1, t1, t2, pp, ex, ey;
  Uint32 *sp0 , *sp1, *spp0 , *spp1, *csp, *dp;
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
  sp0 = sp1 = csp = (Uint32 *) src->pixels;
  sp1++;
  spp0 = spp1 = (Uint32 *) ((Uint8 *) csp + src->pitch);
  spp1++;
  dp = (Uint32 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 2);

  /*
   * Interpolating Zoom 
   */

  /*
   * Scan destination 
   */
  csay = say;
  csax = sax;
  height = dst->h;
  width = dst->w;
  while(height--) {
    DUFFS_LOOP4 (
    {
      /*
       * Interpolate colors 
       */
      ex = (*csax & 0xff);
      ey = (*csay & 0xff);

      cc0 = *sp0;
      cc0 = (cc0 & 0x00ff00ff);
      cc1 = *sp1;
      cc1 = (cc1 & 0x00ff00ff);

      t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      cc0 = *spp0;
      cc0 = (cc0 & 0x00ff00ff);
      cc1 = *spp1;
      cc1 = (cc1 & 0x00ff00ff);

      t2 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      pp = ((((t2 - t1) * ey) >> 8) + t1) & 0x00ff00ff;

      cc0 = *sp0;
      cc1 = *spp0;
      cc0 = ((cc1 & 0xff00) << 8) | ((cc0 & 0xff00) >> 8);
      cc1 = *sp1;
      t1 = *spp1;
      cc1 = ((t1 & 0xff00) << 8) | ((cc1 & 0xff00) >> 8);

      t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      t2 = t1 >> 16;
      t1 &= 0xff;

      pp = pp | ((((((t2 - t1) * ey) >> 8) + t1) & 0xff) << 8);

      /* set dest alpha == 255 */
      *dp = pp | 0xff000000;

      /*
       * Advance source pointers 
       */
      csax++;
      /* cc0 == step */
      cc0 = (*csax >> 8);
      sp0 += cc0;
      sp1 += cc0;
      spp0 += cc0;
      spp1 += cc0;

      /*
       * Advance destination pointer 
       */
      dp++;
    }, width);
    
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint32 *) ((Uint8 *) csp + (*csay >> 8) * src->pitch);
    sp0 = sp1 = csp;
    sp1++;
    spp0 = spp1 = (Uint32 *) ((Uint8 *) csp + src->pitch);
    spp1++;
    csax = sax;
    
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
  Uint32 width, height;
  Uint32 *sax, *say, *csax, *csay;
  register Uint32 cc0, cc1, t1, t2, pp, ex, ey;
  Uint8 *sp0 , *sp1, *spp0 , *spp1, *csp, *dp;
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
  sp0 = sp1 = csp = (Uint8 *) src->pixels;
  sp1 += 3;
  spp0 = spp1 = (Uint8 *) (csp + src->pitch);
  spp1 += 3;
  dp = (Uint8 *) dst->pixels;
  dgap = dst->pitch - dst->w * 3;

  /*
   * Interpolating Zoom 
   */

  /*
   * Scan destination 
   */
  csay = say;
  csax = sax;
  height = dst->h;
  width = dst->w;
  while(height--) {
    DUFFS_LOOP4 (
    {

      /*
       * Interpolate colors 
       */
      ex = (*csax & 0xff);
      ey = (*csay & 0xff);

      cc0 = (sp0[2] << 16) | *sp0;
      cc1 = (sp1[2] << 16) | *sp1;
      t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      cc0 = (spp0[2] << 16) | *spp0;
      cc1 = (spp1[2] << 16) | *spp1;
      t2 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

      pp = ((((t2 - t1) * ey) >> 8) + t1) & 0x00ff00ff;

      cc0 = (spp0[1] << 16) | sp0[1];
      cc1 = (spp1[1] << 16) | spp1[1];
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
      cc0 *= 3;

      sp0 += cc0;
      sp1 += cc0;
      spp0 += cc0;
      spp1 += cc0;

      /*
       * Advance destination pointer 
       */
      dp += 3;
    }, width);

    /*
     * Advance source pointer 
     */
    csay++;
    csp += (*csay >> 8) * src->pitch;
    sp0 = sp1 = csp;
    sp1 += 3;
    spp0 = spp1 = (Uint8 *)(csp + src->pitch);
    spp1 += 3;
    csax = sax;
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
static int AA_ZoomSurfaceFastRGB16(const SDL_Surface *src, SDL_Surface *dst)
{
  Uint32 width, height;
  Uint32 *sax, *say, *csax, *csay;
  register Uint32 cc0, cc1, t1, t2, pp, ex, ey;
  Uint16 *sp0 , *sp1, *spp0 , *spp1, *csp, *dp;
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
  sp0 = sp1 = csp = (Uint16 *) src->pixels;
  sp1++;
  spp0 = spp1 = (Uint16 *) ((Uint8 *) csp + src->pitch);
  spp1++;
  dp = (Uint16 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 1);

  /*
   * Interpolating Zoom 
   */

  /*
   * Scan destination 
   */
  csay = say;
  csax = sax;
  height = dst->h;
  width = dst->w;
  while(height--) {
    DUFFS_LOOP4 (
    {
        /*
	 * Interpolate colors 
	 */
	ex = (*csax & 0xff);
	ey = (*csay & 0xff);

	/* 16 bit 5-6-5 */
        cc0 = *sp0;
	cc0 = /*blue */ (((cc0 >> 11) & 0x1F) << 16) | ((cc0) & 0x1F);	/* red */

	cc1 = *sp1;
	cc1 = /*blue */ (((cc1 >> 11) & 0x1F) << 16) | ((cc1) & 0x1F);	/* red */

	t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

	cc0 = *spp0;
	cc0 = /*blue */ (((cc0 >> 11) & 0x1F) << 16) | ((cc0) & 0x1F);	/* red */

	cc1 = *spp1;
	cc1 = /*blue */ (((cc1 >> 11) & 0x1F) << 16) | ((cc1) & 0x1F);	/* red */

	t2 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

	pp = ((((t2 - t1) * ey) >> 8) + t1) & 0x00ff00ff;

	/* no alpha in 16 bit coding */

	cc0 = *sp0;
	t1 = *spp0;
	cc0 = (((t1 >> 5) & 0x3F) << 16) | ((cc0 >> 5) & 0x3F);	/* Green */

	cc1 = *sp1;
	t1 = *spp1;
	cc1 = (((t1 >> 5) & 0x3F) << 16) | ((cc1 >> 5) & 0x3F);	/* Green */

	t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

	t2 = t1 >> 16;
	t1 &= 0xff;

	pp = pp | ((((((t2 - t1) * ey) >> 8) + t1) & 0xff) << 8);

	*dp = (Uint16) (((pp) & 0x1F) |
			((((pp >> 8) & 0x3F)) << 5) |
			((((pp >> 16) & 0x1F)) << 11));
      
      /*
       * Advance source pointers 
       */
      csax++;
      /* cc0 == step */
      cc0 = (*csax >> 8);
      sp0 += cc0;
      sp1 += cc0;
      spp0 += cc0;
      spp1 += cc0;

      /*
       * Advance destination pointer 
       */
      dp++;
    }, width);
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint16 *) ((Uint8 *) csp + (*csay >> 8) * src->pitch);
    sp0 = sp1 = csp;
    sp1++;
    spp0 = spp1 = (Uint16 *) ((Uint8 *) csp + src->pitch);
    spp1++;
    csax = sax;
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
  Move green to upper 16 bit and cut alpha to 5 bits.
**************************************************************************/
static int AA_ZoomSurfaceFastestRGB16(const SDL_Surface *src, SDL_Surface *dst)
{
  Uint32 width, height;
  Uint32 *sax, *say, *csax, *csay;
  register Uint32 cc0, cc1, t1, t2, pp, ex, ey;
  Uint16 *sp0 , *sp1, *spp0 , *spp1, *csp, *dp;
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
  sp0 = sp1 = csp = (Uint16 *) src->pixels;
  sp1++;
  spp0 = spp1 = (Uint16 *) ((Uint8 *) csp + src->pitch);
  spp1++;
  dp = (Uint16 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 1);

  /*
   * Interpolating Zoom 
   */

  /*
   * Scan destination 
   */
  csay = say;
  csax = sax;
  height = dst->h;
  width = dst->w;
  while(height--) {
    DUFFS_LOOP4 (
    {
        /*
	 * Interpolate colors 
	 */
	ex = (*csax & 0x1f);
	ey = (*csay & 0x1f);

	/* 16 bit 5-6-5 */
      
        cc0 = *sp0;
	cc0 = (cc0 | cc0 << 16) & 0x07e0f81f;

	cc1 = *sp1;
	cc1 = (cc1 | cc1 << 16) & 0x07e0f81f;

	t1 = ((((cc1 - cc0) * ex) >> 5) + cc0) & 0x07e0f81f;

	cc0 = *spp0;
	cc0 = (cc0 | cc0 << 16) & 0x07e0f81f;

	cc1 = *spp1;
	cc1 = (cc1 | cc1 << 16) & 0x07e0f81f;

	t2 = ((((cc1 - cc0) * ex) >> 5) + cc0) & 0x07e0f81f;


	pp = ((((t2 - t1) * ex) >> 5) + t1) & 0x07e0f81f;

	*dp = (Uint16) (pp | pp >> 16);
      
      /*
       * Advance source pointers 
       */
      csax++;
      /* cc0 == step */
      cc0 = (*csax >> 8);
      sp0 += cc0;
      sp1 += cc0;
      spp0 += cc0;
      spp1 += cc0;

      /*
       * Advance destination pointer 
       */
      dp++;
    }, width);
    
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint16 *) ((Uint8 *) csp + (*csay >> 8) * src->pitch);
    sp0 = sp1 = csp;
    sp1++;
    spp0 = spp1 = (Uint16 *) ((Uint8 *) csp + src->pitch);
    spp1++;
    csax = sax;
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
static int AA_ZoomSurfaceFastRGB15(const SDL_Surface *src, SDL_Surface *dst)
{
  Uint32 width, height;
  Uint32 *sax, *say, *csax, *csay;
  register Uint32 cc0, cc1, t1, t2, pp, ex, ey;
  Uint16 *sp0 , *sp1, *spp0 , *spp1, *csp, *dp;
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
  sp0 = sp1 = csp = (Uint16 *) src->pixels;
  sp1++;
  spp0 = spp1 = (Uint16 *) ((Uint8 *) csp + src->pitch);
  spp1++;
  dp = (Uint16 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 1);

  /*
   * Interpolating Zoom 
   */

  /*
   * Scan destination 
   */
  csay = say;
  csax = sax;
  height = dst->h;
  width = dst->w;
  while(height--) {
    DUFFS_LOOP4 (
    {
        /*
	 * Interpolate colors 
	 */
	ex = (*csax & 0xff);
	ey = (*csay & 0xff);

	/* RGB 15bit 5-5-5 */
      
        cc0 = *sp0;
	cc0 = /*blue */ (((cc0 >> 10) & 0x1F) << 16) | ((cc0) & 0x1F);	/* red */

	cc1 = *sp1;
	cc1 = /*blue */ (((cc1 >> 10) & 0x1F) << 16) | ((cc1) & 0x1F);	/* red */

	t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

	cc0 = *spp0;
	cc0 = /*blue */ (((cc0 >> 10) & 0x1F) << 16) | ((cc0) & 0x1F);	/* red */

	cc1 = *spp1;
	cc1 = /*blue */ (((cc1 >> 10) & 0x1F) << 16) | ((cc1) & 0x1F);	/* red */

	t2 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

	pp = ((((t2 - t1) * ey) >> 8) + t1) & 0x00ff00ff;

	/* no alpha in 16 bit coding */

	cc0 = *sp0;
	t1 = *spp0;
	cc0 = (((t1 >> 5) & 0x1F) << 16) | ((cc0 >> 5) & 0x1F);	/* Green */

	cc1 = *sp1;
	t1 = *spp1;
	cc1 = (((t1 >> 5) & 0x1F) << 16) | ((cc1 >> 5) & 0x1F);	/* Green */

	t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

	t2 = t1 >> 16;
	t1 &= 0xff;

	pp = pp | ((((((t2 - t1) * ey) >> 8) + t1) & 0xff) << 8);

	*dp = (Uint16) (((pp) & 0x1F) |
			((((pp >> 8) & 0x1F)) << 5) |
			((((pp >> 16) & 0x1F)) << 10));
      
      /*
       * Advance source pointers 
       */
      csax++;
      /* cc0 == step */
      cc0 = (*csax >> 8);
      sp0 += cc0;
      sp1 += cc0;
      spp0 += cc0;
      spp1 += cc0;

      /*
       * Advance destination pointer 
       */
      dp++;
    }, width);
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint16 *) ((Uint8 *) csp + (*csay >> 8) * src->pitch);
    sp0 = sp1 = csp;
    sp1++;
    spp0 = spp1 = (Uint16 *) ((Uint8 *) csp + src->pitch);
    spp1++;
    csax = sax;
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
  Move green to upper 16 bit and cut alpha to 5 bits.
**************************************************************************/
static int AA_ZoomSurfaceFastestRGB15(const SDL_Surface *src, SDL_Surface *dst)
{
  Uint32 width, height;
  Uint32 *sax, *say, *csax, *csay;
  register Uint32 cc0, cc1, t1, t2, pp, ex, ey;
  Uint16 *sp0 , *sp1, *spp0 , *spp1, *csp, *dp;
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
  sp0 = sp1 = csp = (Uint16 *) src->pixels;
  sp1++;
  spp0 = spp1 = (Uint16 *) ((Uint8 *) csp + src->pitch);
  spp1++;
  dp = (Uint16 *) dst->pixels;
  dgap = dst->pitch - (dst->w << 1);

  /*
   * Interpolating Zoom 
   */

  /*
   * Scan destination 
   */
  csay = say;
  csax = sax;
  height = dst->h;
  width = dst->w;
  while(height--) {
    DUFFS_LOOP4 (
    {
        /*
	 * Interpolate colors 
	 */
	ex = (*csax & 0x1f);
	ey = (*csay & 0x1f);

	/* 15 bit 5-5-5 */
	/* move green to upper 16 bit */
      
        cc0 = *sp0;
	cc0 = (cc0 | cc0 << 16) & 0x03e07c1f;

	cc1 = *sp1;
	cc1 = (cc1 | cc1 << 16) & 0x03e07c1f;

	t1 = ((((cc1 - cc0) * ex) >> 5) + cc0) & 0x03e07c1f;

	cc0 = *spp0;
	cc0 = (cc0 | cc0 << 16) & 0x03e07c1f;

	cc1 = *spp1;
	cc1 = (cc1 | cc1 << 16) & 0x03e07c1f;

	t2 = ((((cc1 - cc0) * ex) >> 5) + cc0) & 0x03e07c1f;


	pp = ((((t2 - t1) * ex) >> 5) + t1) & 0x03e07c1f;

	*dp = (Uint16) (pp | pp >> 16);
      
      /*
       * Advance source pointers 
       */
      csax++;
      /* cc0 == step */
      cc0 = (*csax >> 8);
      sp0 += cc0;
      sp1 += cc0;
      spp0 += cc0;
      spp1 += cc0;

      /*
       * Advance destination pointer 
       */
      dp++;
    }, width);
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint16 *) ((Uint8 *) csp + (*csay >> 8) * src->pitch);
    sp0 = sp1 = csp;
    sp1++;
    spp0 = spp1 = (Uint16 *) ((Uint8 *) csp + src->pitch);
    spp1++;
    csax = sax;
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
#endif

#define GET_R_FROM_PIXEL(pixel, fmt)	\
	(((pixel&fmt->Rmask)>>fmt->Rshift)<<fmt->Rloss)

#define POSITION_R_IN_PIXEL(R, fmt)	\
	(((R>>fmt->Rloss)<<fmt->Rshift)&fmt->Rmask)
	
#define GET_G_FROM_PIXEL(pixel, fmt)	\
	(((pixel&fmt->Gmask)>>fmt->Gshift)<<fmt->Gloss)

#define POSITION_G_IN_PIXEL(G, fmt)	\
	(((G>>fmt->Gloss)<<fmt->Gshift)&fmt->Gmask)

#define GET_B_FROM_PIXEL(pixel, fmt)	\
	(((pixel&fmt->Bmask)>>fmt->Bshift)<<fmt->Bloss)

#define POSITION_B_IN_PIXEL(B, fmt)	\
	(((B>>fmt->Bloss)<<fmt->Bshift)&fmt->Bmask)

#define GET_A_FROM_PIXEL(pixel, fmt)	\
	(((pixel&fmt->Amask)>>fmt->Ashift)<<fmt->Aloss)

#define POSITION_A_IN_PIXEL(A, fmt)	\
	(((A>>fmt->Aloss)<<fmt->Ashift)&fmt->Amask)
	
/**************************************************************************
  N bit Zoomer with anti-aliasing by slow bilinear interpolation.

  Zooms N bit RGB/BGR 'src' surface to 'dst' surface.
  This is most slowest function and is called in no-standart pixel codings.
**************************************************************************/
static int AA_ZoomSurfaceRGBn(const SDL_Surface *src, SDL_Surface *dst)
{
  Uint32 width, height;
  Uint32 *sax, *say, *csax, *csay;
  Uint32 cc0, cc1, t1, t2, pp, ex, ey;
  Uint8 *sp0 , *sp1, *spp0, *spp1, *csp, *dp;
  int dgap, bpp = dst->format->BytesPerPixel;
  
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
  sp0 = sp1 = csp = (Uint8 *) src->pixels;
  sp1 += bpp;
  spp0 = spp1 = (Uint8 *) ((Uint8 *) csp + src->pitch);
  spp1 += bpp;
  dp = (Uint8 *) dst->pixels;
  dgap = dst->pitch - dst->w * bpp;

  /*
   * Interpolating Zoom 
   */

  /*
   * Scan destination 
   */
  csay = say;
  csax = sax;
  height = dst->h;
  width = dst->w;
  while(height--) {
    DUFFS_LOOP4 (
    {
        /*
	 * Interpolate colors 
	 */
	ex = (*csax & 0xff);
	ey = (*csay & 0xff);

        memcpy(&cc0, sp0, bpp);
	cc0 = (GET_B_FROM_PIXEL(cc0, src->format) << 16) |
      					GET_R_FROM_PIXEL(cc0, src->format);
      
        memcpy(&cc1, sp1, bpp);
	cc1 = (GET_B_FROM_PIXEL(cc1, src->format) << 16) |
      					GET_R_FROM_PIXEL(cc1, src->format);

	t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

        memcpy(&cc0, spp0, bpp);
	cc0 = (GET_B_FROM_PIXEL(cc0, src->format) << 16) |
      					GET_R_FROM_PIXEL(cc0, src->format);

	memcpy(&cc1, spp1, bpp);
	cc1 = (GET_B_FROM_PIXEL(cc1, src->format) << 16) |
      					GET_R_FROM_PIXEL(cc1, src->format);

	t2 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;
	pp = ((((t2 - t1) * ey) >> 8) + t1) & 0x00ff00ff;
	
        /* ----------------- */
        memcpy(&cc0, sp0, bpp);
	cc0 = (GET_A_FROM_PIXEL(cc0, src->format) << 16) |
      					GET_G_FROM_PIXEL(cc0, src->format);

        memcpy(&cc1, sp1, bpp);
	cc1 = (GET_A_FROM_PIXEL(cc1, src->format) << 16) |
      					GET_G_FROM_PIXEL(cc1, src->format);

	t1 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

        memcpy(&cc0, spp0, bpp);
	cc0 = (GET_A_FROM_PIXEL(cc0, src->format) << 16) |
      					GET_G_FROM_PIXEL(cc0, src->format);

	memcpy(&cc1, spp1, bpp);
	cc1 = (GET_A_FROM_PIXEL(cc1, src->format) << 16) |
      					GET_G_FROM_PIXEL(cc1, src->format);

	t2 = ((((cc1 - cc0) * ex) >> 8) + cc0) & 0x00ff00ff;

	pp = pp | ((((((t2 - t1) * ey) >> 8) + t1) & 0x00ff00ff) << 8);

	/* in this moment pp is coded as full RGBA8888 then
	   we must convert it to dst pixel codding */
	*dp = POSITION_R_IN_PIXEL((pp & 0xff), dst->format) |
	      POSITION_G_IN_PIXEL(((pp >> 8) & 0xff), dst->format) |
	      POSITION_B_IN_PIXEL(((pp >> 16) & 0xff), dst->format) |
	      POSITION_A_IN_PIXEL(((pp >> 24) & 0xff), dst->format);
      	
      /*
       * Advance source pointers 
       */
      csax++;
      /* cc0 == step */
      cc0 = (*csax >> 8);
      cc0 *= bpp;
      
      sp0 += cc0;
      sp1 += cc0;
      spp0 += cc0;
      spp1 += cc0;

      /*
       * Advance destination pointer 
       */
      dp += bpp;
    }, width);
    
    /*
     * Advance source pointer 
     */
    csay++;
    csp += (*csay >> 8) * src->pitch;
    sp0 = sp1 = csp;
    sp1 += bpp;
    spp0 = spp1 = (Uint8 *)(csp + src->pitch);
    spp1 += bpp;
    csax = sax;
    
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
  32bit Zoomer.

  Zoomes 32bit RGB/BGR 'src' surface to 'dst' surface.
**************************************************************************/
static int zoomSurfaceRGB32(const SDL_Surface *src, SDL_Surface *dst)
{
  Uint32 width, height;
  Uint32 *sax, *say, *csax, *csay;
  register Uint32 *sp, *csp, *dp;
  int dgap, step;
  
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
  csax = sax;
  height = dst->h;
  width = dst->w;
  while(height--) {
    DUFFS_LOOP8 (
    { 
      /*
       * Draw 
       */
      *dp = *sp;
      /*
       * Advance source pointers 
       */
      csax++;
      step = *csax;
      step >>= 16;
      sp += step;
      /*
       * Advance destination pointer 
       */
      dp++;
    }, width);
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint32 *) ((Uint8 *) csp + (*csay >> 16) * src->pitch);
    sp = csp;
    csax = sax;
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
  Uint32 width, height;
  Uint32 *sax, *say, *csax, *csay;
  register Uint8 *sp, *csp, *dp;
  int dgap, step;

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
  csax = sax;
  height = dst->h;
  width = dst->w;
  while(height--) {
    DUFFS_LOOP8 (
    { 
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
      step *= 3;
      sp += step;

      /*
       * Advance destination pointer 
       */
      dp += 3;
    }, width);
    /*
     * Advance source pointer 
     */
    csay++;
    csp += ((*csay >> 16) * src->pitch);
    sp = csp;
    csax = sax;
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
  Uint32 width, height;
  Uint32 *sax, *say, *csax, *csay;
  register Uint16 *sp, *csp, *dp;
  int dgap, step;

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
  csax = sax;
  height = dst->h;
  width = dst->w;
  while(height--) {
    DUFFS_LOOP8 (
    { 
      /*
       * Draw 
       */
      *dp = *sp;
      /*
       * Advance source pointers 
       */
      csax++;
      step = *csax;
      step >>= 16;
      sp += step;
      /*
       * Advance destination pointer 
       */
      dp++;
    }, width);
    /*
     * Advance source pointer 
     */
    csay++;
    csp = (Uint16 *) ((Uint8 *) csp + (*csay >> 16) * src->pitch);
    sp = csp;
    csax = sax;
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
  Uint32 width, height, sx, sy, *sax, *say, *csax, *csay;
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
  if ((sax = (Uint32 *) MALLOC(dst->w * sizeof(Uint32))) == NULL) {
    return (-1);
  }

  if ((say = (Uint32 *) MALLOC(dst->h * sizeof(Uint32))) == NULL) {
    if (sax != NULL) {
      FREE(sax);
    }
    return -1;
  }

  /*
   * Precalculate row increments 
   */
  csx = 0;
  csax = sax;
  for (width = 0; width < dst->w; width++) {
    csx += sx;
    *csax = (csx >> 16);
    csx &= 0xffff;
    csax++;
  }

  csy = 0;
  csay = say;
  for (height = 0; height < dst->h; height++) {
    csy += sy;
    *csay = (csy >> 16);
    csy &= 0xffff;
    csay++;
  }

  csx = 0;
  csax = sax;
  for (width = 0; width < dst->w; width++) {
    csx += *csax;
    csax++;
  }

  csy = 0;
  csay = say;
  for (height = 0; height < dst->h; height++) {
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
   * Zoom
   */
  csay = say;
  csax = sax;
  height = dst->h;
  width = dst->w;
  while(height--) {
    DUFFS_LOOP8 (
    {
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
    }, width);

    /*
     * Advance source pointer (for row) 
     */
    csp += (*csay * src->pitch);
    csay++;
    sp = csp;
    csax = sax;
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
  SDL_Surface *pReal_dst, *pBuf_Src = (SDL_Surface *) pSrc;
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
      if (pReal_dst->format->Gmask == 0xFF00) {
#ifdef HAVE_MMX1
        /* RGBA -> 8-8-8-8 -> 32 bit */
        AA_ZoomSurfaceFastRGBA32(pSrc, pReal_dst);
#else      
        if (pReal_dst->format->Amask) {
	  /* RGBA -> 8-8-8-8 -> 32 bit */
	  AA_ZoomSurfaceFastRGBA32(pSrc, pReal_dst);
        } else {
	  /* no alpha -> 24 bit coding 8-8-8 */
	  AA_ZoomSurfaceFastRGB32(pSrc, pReal_dst);
        }
#endif  
      } else {
	/* no standart pixel coding */
	AA_ZoomSurfaceRGBn(pSrc, pReal_dst);
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
    
    if((pSrc->flags & SDL_SRCCOLORKEY) == SDL_SRCCOLORKEY) {
      SDL_SetColorKey(pReal_dst, SDL_SRCCOLORKEY, pSrc->format->colorkey);
    }
    
    break;
  case 24:
    if (smooth) {
      /*
       *      Call the 24bit transformation routine to do
       *      the zooming (using alpha) with faster AA algoritm
       */
      if (pReal_dst->format->Gmask == 0xFF00) {
	/* pixel is RGB888 */
        AA_ZoomSurfaceFastRGB24(pSrc, pReal_dst);
      } else {
	/* no standart pixel coding */
	AA_ZoomSurfaceRGBn(pSrc, pReal_dst);
      }
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
    
    if((pSrc->flags & SDL_SRCCOLORKEY) == SDL_SRCCOLORKEY) {
      SDL_SetColorKey(pReal_dst, SDL_SRCCOLORKEY, pSrc->format->colorkey);
    }
    
    break;
  case 16:
    switch (smooth) {
    case 1:
#ifdef HAVE_MMX1
    case 2:
#endif      
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
	/* no standart pixel coding */
	AA_ZoomSurfaceRGBn(pSrc, pReal_dst);
      }
      /*
       * Turn on source-alpha support 
       */
      SDL_SetAlpha(pReal_dst, SDL_SRCALPHA, 255);

      break;
#ifndef HAVE_MMX1      
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
	/* no standart pixel coding */
	AA_ZoomSurfaceRGBn(pSrc, pReal_dst);
      }

      /*
       * Turn on source-alpha support 
       */
      SDL_SetAlpha(pReal_dst, SDL_SRCALPHA, 255);

      break;
#endif      
    default:

      /*
       *      Call the 16bit transformation routine
       *      to do the zooming
       */
      zoomSurfaceRGB16(pSrc, pReal_dst);

      break;
    }
    
    if((pSrc->flags & SDL_SRCCOLORKEY) == SDL_SRCCOLORKEY) {
      SDL_SetColorKey(pReal_dst, SDL_SRCCOLORKEY, pSrc->format->colorkey);
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
    FREESURFACE(pReal_dst);
    break;
  }

  /*
   * Unlock source surface 
   */
  unlock_surf(pBuf_Src);

  /*
   * Return destination surface 
   */
  return pReal_dst;
}
