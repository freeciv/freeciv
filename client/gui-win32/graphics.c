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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>  
#include <stdlib.h>
#include <windows.h>
#include <png.h>

#include "game.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "support.h"
#include "unit.h"
#include "version.h"
 
#include "climisc.h"
#include "colors.h"
#include "gui_main.h"
#include "mapview_g.h"
#include "sprite.h"
#include "tilespec.h"   

#include "graphics.h"
#define CACHE_SIZE 64

struct Bitmap_cache {
  BITMAP *src;
  HBITMAP hbmp;
};

extern HINSTANCE freecivhinst;

static struct Bitmap_cache bitmap_cache[CACHE_SIZE];
static int cache_id_count = 0;

HCURSOR cursors[CURSOR_LAST];

struct Sprite *intro_gfx_sprite = NULL;
struct Sprite *radar_gfx_sprite = NULL;

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
  return TRUE;
}

/****************************************************************************
  Load the introductory graphics.
****************************************************************************/
void load_intro_gfx(void)
{
  intro_gfx_sprite = load_gfxfile(tileset_main_intro_filename(tileset));
  radar_gfx_sprite = load_gfxfile(tileset_mini_intro_filename(tileset));
}

/**************************************************************************
  Load the cursors (mouse substitute sprites), including a goto cursor,
  an airdrop cursor, a nuke cursor, and a patrol cursor.
**************************************************************************/
void
load_cursors(void)
{
  enum cursor_type cursor;
  ICONINFO ii;

  /* For some reason win32 lets you enter a cursor size, which
   * only works as long as it's this size. */
  int width = GetSystemMetrics(SM_CXCURSOR);
  int height = GetSystemMetrics(SM_CYCURSOR);

  BITMAP bmp;

  unsigned char *xor_bmp = fc_malloc(width * height * 4);
  unsigned char *and_bmp = fc_malloc(width * height * 4);

  bmp.bmType = 0;
  bmp.bmWidth = width;
  bmp.bmHeight = height;
  bmp.bmWidthBytes = width << 2;
  bmp.bmPlanes = 1;
  bmp.bmBitsPixel = 32;

  ii.fIcon = FALSE;

  for (cursor = 0; cursor < CURSOR_LAST; cursor++) {
    int hot_x, hot_y;
    int x, y;
    int minwidth, minheight;
    struct Sprite *sprite = get_cursor_sprite(tileset, cursor, &hot_x, &hot_y);
    unsigned char *src;

    ii.xHotspot = hot_x;
    ii.yHotspot = hot_y;    

    memset(xor_bmp, 0,   width * height * 4);
    memset(and_bmp, 255, width * height * 4);

    minwidth = MIN(width, sprite->img.bmWidth);
    minheight = MIN(height, sprite->img.bmHeight);

    for (y = 0; y < minheight; y++) {
      src = (unsigned char *)sprite->img.bmBits + sprite->img.bmWidthBytes * y;
      for (x = 0; x < minwidth; x++) {
	int byte_pos = (x + y * width) * 4;
	
	if (src[3] > 128) {
	  xor_bmp[byte_pos]     = src[0];
	  xor_bmp[byte_pos + 1] = src[1];
	  xor_bmp[byte_pos + 2] = src[2];
	  xor_bmp[byte_pos + 3] = 255;
	  and_bmp[byte_pos]     = 0;
	  and_bmp[byte_pos + 1] = 0;
	  and_bmp[byte_pos + 2] = 0;
	  and_bmp[byte_pos + 3] = 0;
	}

	src += 4;
      }
    }

    bmp.bmBits = and_bmp;
    ii.hbmMask = BITMAP2HBITMAP(&bmp);
    bmp.bmBits = xor_bmp;
    ii.hbmColor = BITMAP2HBITMAP(&bmp);

    cursors[cursor] = CreateIconIndirect(&ii);

    DeleteObject(ii.hbmMask);
    DeleteObject(ii.hbmColor);
  }

  free(xor_bmp);
  free(and_bmp);
}

/****************************************************************************
  Frees the introductory sprites.
****************************************************************************/
void free_intro_radar_sprites(void)
{
  if (intro_gfx_sprite) {
    free_sprite(intro_gfx_sprite);
    intro_gfx_sprite = NULL;
  }
  if (radar_gfx_sprite) {
    free_sprite(radar_gfx_sprite);
    radar_gfx_sprite = NULL;
  }
}

/*************************************************************************
  Create a device dependent bitmap from a device independent bitmap.

  This function makes GDI handle all the transformations, so we can store
  all bitmaps (besides masks) in an internal 32-bit format.
 *************************************************************************/
HBITMAP BITMAP2HBITMAP(BITMAP *bmp)
{
  HDC hdc;
  LPBITMAPINFO lpbi;
  HBITMAP hbmp;

  lpbi = fc_malloc(sizeof(BITMAPINFOHEADER) + 2 * sizeof(RGBQUAD));

  lpbi->bmiHeader.biSize          = sizeof(BITMAPINFOHEADER);
  lpbi->bmiHeader.biWidth         = bmp->bmWidth;
  lpbi->bmiHeader.biHeight        = -bmp->bmHeight;
  lpbi->bmiHeader.biPlanes        = bmp->bmPlanes;
  lpbi->bmiHeader.biBitCount      = bmp->bmBitsPixel;

  lpbi->bmiHeader.biCompression   = BI_RGB;
  lpbi->bmiHeader.biSizeImage     = 0;
  lpbi->bmiHeader.biXPelsPerMeter = 0;
  lpbi->bmiHeader.biYPelsPerMeter = 0;
  lpbi->bmiHeader.biClrUsed       = 0;
  lpbi->bmiHeader.biClrImportant  = 0;

  if (bmp->bmBitsPixel == 1) {
    lpbi->bmiColors[0].rgbBlue  = 0;
    lpbi->bmiColors[0].rgbGreen = 0;
    lpbi->bmiColors[0].rgbRed   = 0;
    lpbi->bmiColors[1].rgbBlue  = 255;
    lpbi->bmiColors[1].rgbGreen = 255;
    lpbi->bmiColors[1].rgbRed   = 255;
  }

  hdc = GetDC(root_window);
  hbmp = CreateDIBitmap(hdc, &lpbi->bmiHeader, CBM_INIT, bmp->bmBits, lpbi,
			0);
  ReleaseDC(root_window, hdc);

  free(lpbi);

  return hbmp;
}

/**************************************************************************
  Retrieve an image from the HBITMAP cache, or if it is not there, place
  it into the cache and return it.
**************************************************************************/
HBITMAP getcachehbitmap(BITMAP *bmp, int *cache_id)
{
  struct Bitmap_cache *bmpc;

  bmpc = &bitmap_cache[*cache_id];
  if ((*cache_id == -1) || (bmpc->src != bmp)) {
    cache_id_count++;
    cache_id_count %= CACHE_SIZE;
    *cache_id = cache_id_count;
    bmpc = &bitmap_cache[cache_id_count];
    DeleteObject(bmpc->hbmp);
    bmpc->src = bmp;
    bmpc->hbmp = BITMAP2HBITMAP(bmp);
  } 
  return bmpc->hbmp;
}

/**************************************************************************
  Premultiply the colors in a bitmap by the per-pixel alpha value, so
  alpha blending can be done with one less multiplication.

  The AlphaBlend function requires this.
**************************************************************************/
BITMAP premultiply_alpha(BITMAP bmp)
{
  BITMAP alphabmp;
  unsigned char *src, *dst;
  int row, col;

  alphabmp = bmp;
  alphabmp.bmBits = fc_malloc(bmp.bmWidthBytes * bmp.bmHeight);

  for (row = 0; row < bmp.bmHeight; row++) {
    src = (unsigned char *)bmp.bmBits + bmp.bmWidthBytes * row;
    dst = (unsigned char *)alphabmp.bmBits + alphabmp.bmWidthBytes * row;
    for (col = 0; col < bmp.bmWidth; col++) {
      dst[0] = src[0] * src[3] / 255;
      dst[1] = src[1] * src[3] / 255;
      dst[2] = src[2] * src[3] / 255;
      dst[3] = src[3];
      src += 4;
      dst += 4;
    }
  }
    
  return alphabmp;
}

/**************************************************************************
  Generate a mask from a bitmap's alpha channel, using a different dither
  function depending on transparency level.
**************************************************************************/
BITMAP generate_mask(BITMAP bmp)
{
  BITMAP mask;
  unsigned char *src, *dst;
  int row, col;

  mask.bmType = 0;
  mask.bmWidth = bmp.bmWidth;
  mask.bmHeight = bmp.bmHeight;
  mask.bmWidthBytes = (bmp.bmWidth + 7) / 8;
  mask.bmPlanes = 1;
  mask.bmBitsPixel = 1;

  mask.bmBits = fc_malloc(mask.bmWidthBytes * mask.bmHeight);
  memset((void *)mask.bmBits, 0, mask.bmWidthBytes * mask.bmHeight);

  for (row = 0; row < bmp.bmHeight; row++) {
    src = (unsigned char *)bmp.bmBits + bmp.bmWidthBytes * row;
    dst = (unsigned char *)mask.bmBits + mask.bmWidthBytes * row;
    for (col = 0; col < bmp.bmWidth; col++) {
      int byte_pos = col / 8;
      int bit_pos  = col % 8;
      bool pixel = FALSE;
      src += 3;
      if (*src > 255 * 4 / 5) {
	pixel = FALSE;
      } else if (*src > 255 * 3 / 5) {
	if ((row + col * 2) % 4 == 0) {
	  pixel = TRUE;
	} else {
	  pixel = FALSE;
	}
      } else if (*src > 255 * 2 / 5) {
	if ((row + col) % 2 == 0) {
	  pixel = TRUE;
	} else {
	  pixel = FALSE;
	}
      } else if (*src > 255 * 1 / 5) {
	if ((row + col * 2) % 4 == 0) {
	  pixel = FALSE;
	} else {
	  pixel = TRUE;
	}
      } else {
	  pixel = TRUE;
      }
      if (pixel) {
	dst[byte_pos] |= (128 >> bit_pos);
      }
      src++;
    }
  }
  return mask;
}
