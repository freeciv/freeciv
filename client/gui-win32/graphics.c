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

struct sprite *intro_gfx_sprite = NULL;
struct sprite *radar_gfx_sprite = NULL;

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

  BITMAP *xor_bmp, *and_bmp;

  xor_bmp = bmp_new(width, height);
  and_bmp = bmp_new(width, height);

  ii.fIcon = FALSE;

  for (cursor = 0; cursor < CURSOR_LAST; cursor++) {
    int hot_x, hot_y;
    int x, y;
    int minwidth, minheight;
    struct sprite *sprite = get_cursor_sprite(tileset, cursor, &hot_x, &hot_y);

    ii.xHotspot = MIN(hot_x, width);
    ii.yHotspot = MIN(hot_y, height);

    memset(xor_bmp->bmBits, 0,   width * height * 4);
    memset(and_bmp->bmBits, 255, width * height * 4);

    minwidth  = MIN(width,  sprite->img->bmWidth);
    minheight = MIN(height, sprite->img->bmHeight);

    for (y = 0; y < minheight; y++) {
      BYTE *src = (BYTE *)sprite->img->bmBits + sprite->img->bmWidthBytes * y;
      BYTE *xor_dst = (BYTE *)xor_bmp->bmBits + xor_bmp->bmWidthBytes * y;
      BYTE *and_dst = (BYTE *)and_bmp->bmBits + and_bmp->bmWidthBytes * y;
      for (x = 0; x < minwidth; x++) {
	if (src[3] > 128) {
	  *xor_dst++ = src[0];
	  *xor_dst++ = src[1];
	  *xor_dst++ = src[2];
	  *xor_dst++ = 255;
	  *and_dst++ = 0;
	  *and_dst++ = 0;
	  *and_dst++ = 0;
	  *and_dst++ = 0;
	} else {
	  and_dst += 4;
	  xor_dst += 4;
	}
	src += 4;
      }
    }

    ii.hbmMask = BITMAP2HBITMAP(and_bmp);
    ii.hbmColor = BITMAP2HBITMAP(xor_bmp);

    cursors[cursor] = CreateIconIndirect(&ii);

    DeleteObject(ii.hbmMask);
    DeleteObject(ii.hbmColor);
  }

  bmp_free(xor_bmp);
  bmp_free(and_bmp);
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
  Create a device independent bitmap.
*************************************************************************/
BITMAP *bmp_new(int width, int height)
{
  BITMAP *bmp = fc_malloc(sizeof(*bmp));

  bmp->bmType       = 0;
  bmp->bmWidth      = width;
  bmp->bmHeight     = height;
  bmp->bmWidthBytes = width << 2;
  bmp->bmPlanes     = 1;
  bmp->bmBitsPixel  = 32;
  bmp->bmBits       = fc_malloc(width * height * 4);

  return bmp;
}


/*************************************************************************
  Release all data held by a device independent bitmap.
*************************************************************************/
void bmp_free(BITMAP *bmp)
{
  free(bmp->bmBits);
  free(bmp);
}


/*************************************************************************
  Create a device independent bitmap from a device dependent bitmap.

  This function makes GDI handle all the transformations, so we can store
  all bitmaps (besides masks) in an internal 32-bit format.
*************************************************************************/
BITMAP *HBITMAP2BITMAP(HBITMAP hbmp)
{
  HDC hdc;
  LPBITMAPINFO lpbi;
  BITMAP *bmp = fc_malloc(sizeof(*bmp));

  GetObject(hbmp, sizeof(BITMAP), bmp);

  lpbi = fc_malloc(sizeof(BITMAPINFOHEADER) + 2 * sizeof(RGBQUAD));

  lpbi->bmiHeader.biSize          = sizeof(BITMAPINFOHEADER);
  lpbi->bmiHeader.biWidth         = bmp->bmWidth;
  lpbi->bmiHeader.biHeight        = -abs(bmp->bmHeight);
  lpbi->bmiHeader.biPlanes        = 1;
  lpbi->bmiHeader.biBitCount      = 32;

  lpbi->bmiHeader.biCompression   = BI_RGB;
  lpbi->bmiHeader.biSizeImage     = 0;
  lpbi->bmiHeader.biXPelsPerMeter = 0;
  lpbi->bmiHeader.biYPelsPerMeter = 0;
  lpbi->bmiHeader.biClrUsed       = 0;
  lpbi->bmiHeader.biClrImportant  = 0;

  bmp->bmWidthBytes = bmp->bmWidth << 2;
  bmp->bmPlanes     = 1;
  bmp->bmBitsPixel  = 32;
  bmp->bmBits       = fc_malloc(bmp->bmHeight * bmp->bmWidthBytes);

  hdc = GetDC(root_window);
  GetDIBits(hdc, hbmp, 0, bmp->bmHeight, bmp->bmBits, lpbi, DIB_RGB_COLORS);
  ReleaseDC(root_window, hdc);

  return bmp;
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
  hbmp = CreateDIBitmap(hdc, &lpbi->bmiHeader, CBM_INIT, bmp->bmBits, lpbi, 0);
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
  if ((*cache_id == -1) || (bmpc->src != bmp->bmBits)) {
    cache_id_count++;
    cache_id_count %= CACHE_SIZE;
    *cache_id = cache_id_count;
    bmpc = &bitmap_cache[cache_id_count];
    DeleteObject(bmpc->hbmp);
    bmpc->src = bmp->bmBits;
    bmpc->hbmp = BITMAP2HBITMAP(bmp);
  } 
  return bmpc->hbmp;
}

/**************************************************************************
  Test whether a bitmap needs a mask, ie if its alpha channel
  contains values other than 255.
**************************************************************************/
bool bmp_test_mask(BITMAP *bmp)
{
  int row, col;
  BYTE *src;

  for (row = 0; row < bmp->bmHeight; row++) {
    src = (BYTE *)bmp->bmBits + bmp->bmWidthBytes * row;
    for (col = 0; col < bmp->bmWidth; col++) {
      if (src[3] != 255) {
	return TRUE;
      }
      src += 4;
    }
  }
  return FALSE;
}

/**************************************************************************
  Test whether a bitmap needs alpha blending, ie if its alpha channel
  contains values other than 0 or 255.
**************************************************************************/
bool bmp_test_alpha(BITMAP *bmp)
{
  int row, col;
  BYTE *src;

  for (row = 0; row < bmp->bmHeight; row++) {
    src = (BYTE *)bmp->bmBits + bmp->bmWidthBytes * row;
    for (col = 0; col < bmp->bmWidth; col++) {
      if ((src[3] != 0) && (src[3] != 255)) {
	return TRUE;
      }
      src += 4;
    }
  }
  return FALSE;
}


/**************************************************************************
  Premultiply the colors in a bitmap by the per-pixel alpha value, so
  alpha blending can be done with one less multiplication.

  The AlphaBlend function requires this.
**************************************************************************/
BITMAP *bmp_premult_alpha(BITMAP *bmp)
{
  BITMAP *alpha;
  BYTE *src, *dst;
  int row, col;

  alpha = bmp_new(bmp->bmWidth, bmp->bmHeight);

  for (row = 0; row < bmp->bmHeight; row++) {
    src = (BYTE *)bmp->bmBits + bmp->bmWidthBytes * row;
    dst = (BYTE *)alpha->bmBits + alpha->bmWidthBytes * row;
    for (col = 0; col < bmp->bmWidth; col++) {
      dst[0] = src[0] * src[3] / 255;
      dst[1] = src[1] * src[3] / 255;
      dst[2] = src[2] * src[3] / 255;
      dst[3] = src[3];
      src += 4;
      dst += 4;
    }
  }
    
  return alpha;
}

/**************************************************************************
  Generate a mask from a bitmap's alpha channel, using a different dither
  function depending on transparency level.
**************************************************************************/
BITMAP *bmp_generate_mask(BITMAP *bmp)
{
  BITMAP *mask = fc_malloc(sizeof(*mask));
  BYTE *src, *dst;
  int row, col, bit_pos;

  mask->bmType = 0;
  mask->bmWidth = bmp->bmWidth;
  mask->bmHeight = bmp->bmHeight;
  mask->bmWidthBytes = (bmp->bmWidth + 7) / 8;
  mask->bmPlanes = 1;
  mask->bmBitsPixel = 1;

  mask->bmBits = fc_malloc(mask->bmWidthBytes * mask->bmHeight);
  memset(mask->bmBits, 0, mask->bmWidthBytes * mask->bmHeight);

  src = (BYTE *)bmp->bmBits;

  for (row = 0; row < bmp->bmHeight; row++) {
    dst = (BYTE *)mask->bmBits + mask->bmWidthBytes * row;
    bit_pos = 0;
    for (col = 0; col < bmp->bmWidth; col++) {
      bool pixel = FALSE;
      BYTE alpha = src[3];
      if (alpha > 255 * 4 / 5) {
	pixel = FALSE;
      } else if (alpha > 255 * 3 / 5) {
	if ((row + col * 2) % 4 == 0) {
	  pixel = TRUE;
	} else {
	  pixel = FALSE;
	}
      } else if (alpha > 255 * 2 / 5) {
	if ((row + col) % 2 == 0) {
	  pixel = TRUE;
	} else {
	  pixel = FALSE;
	}
      } else if (alpha > 255 * 1 / 5) {
	if ((row + col * 2) % 4 == 0) {
	  pixel = FALSE;
	} else {
	  pixel = TRUE;
	}
      } else {
	  pixel = TRUE;
      }
      if (pixel) {
	*dst |= (128 >> bit_pos);
      }
      bit_pos++;
      bit_pos %= 8;
      if (bit_pos == 0) {
	dst++;
      }
      src += 4;
    }
  }


  return mask;
}


/**************************************************************************
  Create a cropped version of the given bitmap.
**************************************************************************/
BITMAP *bmp_crop(BITMAP *bmp, int src_x, int src_y, int width, int height)
{
  int row, col;
  BITMAP *crop;
  BYTE *src, *dst;

  crop = bmp_new(width, height);
  dst = crop->bmBits;

  for (row = 0; row < height; row++) {
    src = (BYTE *)bmp->bmBits + bmp->bmWidthBytes * (row + src_y) + 4 * src_x;
    for (col = 0; col < width; col++) {
      *dst++ = *src++;
      *dst++ = *src++;
      *dst++ = *src++;
      *dst++ = *src++;
    }
  }

  return crop;
}

/**************************************************************************
  Combine a bitmap with the alpha channel of another. 
  The second bitmap is offset by the given coordinates.
**************************************************************************/
BITMAP *bmp_blend_alpha(BITMAP *bmp, BITMAP *mask,
			int offset_x, int offset_y)
{
  BYTE *src_bmp, *src_mask, *dst;
  int row, col;
  BITMAP *combine;

  combine = bmp_new(bmp->bmWidth, bmp->bmHeight);

  dst = combine->bmBits;
  for (row = 0; row < bmp->bmHeight; row++) {
    src_bmp  = (BYTE *)bmp->bmBits + bmp->bmWidthBytes * row;
    src_mask = (BYTE *)mask->bmBits + mask->bmWidthBytes * (row + offset_y)
	       + 4 * offset_x;
    for (col = 0; col < bmp->bmWidth; col++) {
      *dst++ = *src_bmp++;
      *dst++ = *src_bmp++;
      *dst++ = *src_bmp++;
      *dst++ = *src_bmp++ * src_mask[3] / 255;
      src_mask += 4;
    }
  }

  return combine;
}

/**************************************************************************
  Create a dimmed version of this bitmap, according to the value given.
**************************************************************************/
BITMAP *bmp_fog(BITMAP *bmp, int brightness)
{
  BITMAP *fog;
  int bmpsize, i;
  BYTE *src, *dst;

  fog = bmp_new(bmp->bmWidth, bmp->bmHeight);
  bmpsize = bmp->bmHeight * bmp->bmWidthBytes;

  src = bmp->bmBits;
  dst = fog->bmBits;
  
  for (i = 0; i < bmpsize; i += 4) {
    *dst++ = *src++ * brightness / 100;
    *dst++ = *src++ * brightness / 100;
    *dst++ = *src++ * brightness / 100;
    *dst++ = *src++;
  }

  return fog;
}


/**************************************************************************
  Load a PNG file into a bitmap.
**************************************************************************/
BITMAP *bmp_load_png(const char *filename)
{
  png_structp pngp;
  png_infop infop;
  png_uint_32 sig_read = 0;
  png_int_32 width, height, row, col;
  int bit_depth, color_type, interlace_type;
  FILE *fp;

  png_bytep *row_pointers;
   
  BITMAP *bmp;
  int has_mask;
  BYTE *src, *dst;

  if (!(fp = fopen(filename, "rb"))) {
    MessageBox(NULL, "failed reading", filename, MB_OK);
    freelog(LOG_FATAL, "Failed reading PNG file: %s", filename);
    exit(EXIT_FAILURE);
  }
    
  if (!(pngp = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL,
				      NULL))) {

    freelog(LOG_FATAL, "Failed creating PNG struct");
    exit(EXIT_FAILURE);
  }
 
  if (!(infop = png_create_info_struct(pngp))) {
    freelog(LOG_FATAL, "Failed creating PNG struct");
    exit(EXIT_FAILURE);
  }
   
  if (setjmp(pngp->jmpbuf)) {
    freelog(LOG_FATAL, "Failed while reading PNG file: %s", filename);
    exit(EXIT_FAILURE);
  }

  png_init_io(pngp, fp);
  png_set_sig_bytes(pngp, sig_read);

  png_read_info(pngp, infop);

  png_set_strip_16(pngp);
  png_set_gray_to_rgb(pngp);
  png_set_packing(pngp);
  png_set_palette_to_rgb(pngp);
  png_set_tRNS_to_alpha(pngp);
  png_set_filler(pngp, 0xFF, PNG_FILLER_AFTER);
  png_set_bgr(pngp);

  png_read_update_info(pngp, infop);
  png_get_IHDR(pngp, infop, &width, &height, &bit_depth, &color_type,
	       &interlace_type, NULL, NULL);
  
  has_mask = (color_type & PNG_COLOR_MASK_ALPHA);

  row_pointers = fc_malloc(sizeof(png_bytep) * height);

  for (row = 0; row < height; row++)
    row_pointers[row] = fc_malloc(png_get_rowbytes(pngp, infop));

  png_read_image(pngp, row_pointers);
  png_read_end(pngp, infop);
  fclose(fp);

  bmp = bmp_new(width, height);

  if (has_mask) {
    for (row = 0, dst = bmp->bmBits; row < height; row++) {
      for (col = 0, src = row_pointers[row]; col < width; col++) {
	*dst++ = *src++;
	*dst++ = *src++;
	*dst++ = *src++;
	*dst++ = *src++;
      }
    }
  } else {
    for (row = 0, dst = bmp->bmBits; row < height; row++) {
      for (col = 0, src = row_pointers[row]; col < width; col++) {
	*dst++ = *src++;
	*dst++ = *src++;
	*dst++ = *src++;
	*dst++ = 255;
	src++;
      }
    } 
  }

  for (row = 0; row < height; row++)
    free(row_pointers[row]);
  free(row_pointers);
  png_destroy_read_struct(&pngp, &infop, NULL);

  return bmp;
}


/**************************************************************************
  Blend a bitmap into a device context, with per pixel alpha.  A fallback
  for when AlphaBlend() is not available.

  FIXME: make less slow.
**************************************************************************/
void blend_bmp_to_hdc(HDC hdc, int dst_x, int dst_y, int w, int h,
		      BITMAP *bmp, int src_x, int src_y)
{
  int row, col;
  for (row = dst_y; row < dst_y + h; row++) {
    BYTE *src = (BYTE *)bmp->bmBits + bmp->bmWidthBytes * (src_y + row - dst_y)
		+ 4 * src_x;
    for (col = dst_x; col < dst_x + w; col++) {
      COLORREF cr;
      BYTE r, g, b;

      if (src[3] == 255) {
	SetPixel(hdc, col, row, RGB(src[0], src[1], src[2]));
      } else if (src[3] != 0) {
	cr = GetPixel(hdc, col, row);

	r = (src[0] + GetRValue(cr) * (256 - src[3])) >> 8;
	g = (src[1] + GetGValue(cr) * (256 - src[3])) >> 8;
	b = (src[2] + GetBValue(cr) * (256 - src[3])) >> 8;

	SetPixel(hdc, col, row, RGB(r, g, b));
      }
      src += 4;
    } 
  }
}
