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
#include "tilespec.h"   

#include "graphics.h"
#define CACHE_SIZE 64

struct Bitmap_cache {
  BITMAP *src;
  HBITMAP hbmp;
};

extern HINSTANCE freecivhinst;

static struct Bitmap_cache bitmap_cache[CACHE_SIZE];
static int cache_id_count=0;
static SPRITE *sprcache;
static HBITMAP bitmapcache;
SPRITE *intro_gfx_sprite=NULL;
SPRITE *radar_gfx_sprite=NULL;
static HBITMAP stipple;
static HBITMAP fogmask;

HCURSOR cursors[CURSOR_LAST];

extern bool have_AlphaBlend;
extern bool enable_alpha;

extern BOOL (WINAPI * AlphaBlend)(HDC,int,int,int,int,HDC,int,int,int,int,BLENDFUNCTION);

/**************************************************************************

**************************************************************************/
void
load_intro_gfx(void)
{
  intro_gfx_sprite=load_gfxfile(tileset_main_intro_filename(tileset));
  radar_gfx_sprite = load_gfxfile(tileset_mini_intro_filename(tileset));     
}

/**************************************************************************

**************************************************************************/
void
load_cursors(void)
{
  enum cursor_type cursor;

  /* For some reason win32 lets you enter a cursor size, which
   * only works as long as it's this size. */
  int width = GetSystemMetrics(SM_CXCURSOR);
  int height = GetSystemMetrics(SM_CYCURSOR);

  unsigned char *xor_bmp = fc_malloc(width * height / 8);
  unsigned char *and_bmp = fc_malloc(width * height / 8);

  for (cursor = 0; cursor < CURSOR_LAST; cursor++) {
    int hot_x, hot_y;
    int x, y;
    int minwidth, minheight;
    struct Sprite *sprite = get_cursor_sprite(tileset, cursor, &hot_x, &hot_y);
    unsigned char *src;

    for (x = 0; x < width * height / 8; x++) {
      xor_bmp[x] = 0;
    }

    for (x = 0; x < width * height / 8; x++) {
      and_bmp[x] = 255;
    }

    minwidth = MIN(width, sprite->img.bmWidth);
    minheight = MIN(height, sprite->img.bmHeight);

    for (y = 0; y < minheight; y++) {
      src = (unsigned char *)sprite->img.bmBits + sprite->img.bmWidthBytes * y;
      for (x = 0; x < minwidth; x++) {
	int byte_pos = (x + y * width) / 8;
	int bit_pos  = (x + y * width) % 8;
	
	if (src[3] > 128) {
	  if (src[0] + src[1] + src[2] > 128) {
	    xor_bmp[byte_pos] |= (128 >> bit_pos);
	  }
	  and_bmp[byte_pos] &= ~(128 >> bit_pos);
	}

	src += 4;
      }
    }

    cursors[cursor] = CreateCursor(freecivhinst, hot_x, hot_y, width, height,
				   and_bmp, xor_bmp);
  }

  free(xor_bmp);
  free(and_bmp);
}

/**************************************************************************

**************************************************************************/
void
free_intro_radar_sprites(void)
{
  if (intro_gfx_sprite)
    {
      free_sprite(intro_gfx_sprite);
      intro_gfx_sprite=NULL;
    }
  if (radar_gfx_sprite)
    {
      free_sprite(radar_gfx_sprite);
      radar_gfx_sprite=NULL;
    }
}

/**************************************************************************

**************************************************************************/
const char **
gfx_fileextensions(void)
{
  static const char *ext[] =
  {
    "png",
    NULL
  };

  return ext;
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
static HBITMAP getcachehbitmap(BITMAP *bmp, int *cache_id)
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
static BITMAP premultiply_alpha(BITMAP bmp)
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
static BITMAP generate_mask(BITMAP bmp)
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

/****************************************************************************
  Create a new sprite by cropping and taking only the given portion of
  the image.
****************************************************************************/
struct Sprite *crop_sprite(struct Sprite *sprsrc,
			   int x, int y, int width, int height,
			   struct Sprite *sprmask,
			   int mask_offset_x, int mask_offset_y)
{
  SPRITE *mysprite = NULL;
  unsigned char *srcimg, *dstimg, *mskimg;
  int row, col;

  mysprite = fc_malloc(sizeof(struct Sprite));
  mysprite->img_cache_id = -1;
  mysprite->fog_cache_id = -1;
  mysprite->mask_cache_id = -1;
  mysprite->pmimg_cache_id = -1;
  mysprite->width = width;
  mysprite->height = height;
  mysprite->has_mask = FALSE;
  mysprite->has_fog = FALSE;
  mysprite->has_pmimg = FALSE;
  mysprite->alphablend = FALSE;

  mysprite->img.bmType = 0;
  mysprite->img.bmWidth = width;
  mysprite->img.bmHeight = height;
  mysprite->img.bmWidthBytes = width << 2;
  mysprite->img.bmPlanes = 1;
  mysprite->img.bmBitsPixel = 32;
  mysprite->img.bmBits = fc_malloc(width * height * 4);

  for (row = 0; row < height; row++) {
    srcimg = (unsigned char *)sprsrc->img.bmBits
	     + sprsrc->img.bmWidthBytes * (row + y) + 4 * x;
    dstimg = (unsigned char *)mysprite->img.bmBits
	     + mysprite->img.bmWidthBytes * row;
    for (col = 0; col < width; col++) {
      *dstimg++ = *srcimg++;
      *dstimg++ = *srcimg++;
      *dstimg++ = *srcimg++;
      *dstimg++ = *srcimg++;
    }
  }

  if (sprmask && sprmask->has_mask) {
    for (row = 0; row < height; row++) {
      mskimg = (unsigned char *)sprmask->img.bmBits
	       + sprmask->img.bmWidthBytes * (row + y - mask_offset_y)
	       + 4 * (x - mask_offset_x);
      dstimg = (unsigned char *)mysprite->img.bmBits
	       + mysprite->img.bmWidthBytes * row;
      for (col = 0; col < width; col++) {
	dstimg += 3;
	mskimg += 3;
	*dstimg = *dstimg * *mskimg / 255;
	dstimg++;
	mskimg++;
      }
    }
  }

  for (row = 0; row < height; row++) {
    dstimg = (unsigned char *)mysprite->img.bmBits
	     + mysprite->img.bmWidthBytes * row;
    for (col = 0; col < width; col++) {
      dstimg += 3;
      if ((*dstimg != 0) && (*dstimg != 255)) {
	mysprite->alphablend = TRUE;
      }
      dstimg++;
    }
  }

  if ((sprmask && sprmask->has_mask) || sprsrc->has_mask) {
    mysprite->has_mask = TRUE;
    mysprite->mask = generate_mask(mysprite->img);
  }

  return mysprite;
}

/****************************************************************************
  Find the dimensions of the sprite.
****************************************************************************/
void get_sprite_dimensions(struct Sprite *sprite, int *width, int *height)
{
  *width = sprite->width;
  *height = sprite->height;
}

/**************************************************************************

***************************************************************************/
void init_fog_bmp(void)
{
  int x,y;
  HBITMAP old;
  HDC hdc;
  hdc = CreateCompatibleDC(NULL);
  if (stipple) {
    DeleteObject(stipple);
  }
  if (fogmask) {
    DeleteObject(fogmask);
  }
  stipple = CreateCompatibleBitmap(hdc, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
  fogmask = CreateCompatibleBitmap(hdc, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
  old = SelectObject(hdc, stipple);
  BitBlt(hdc, 0, 0, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT, NULL, 0, 0, BLACKNESS);
  for(x = 0; x < NORMAL_TILE_WIDTH; x++) {
    for(y = 0; y < NORMAL_TILE_HEIGHT; y++) {
      if ((x + y) & 1) {
	SetPixel(hdc, x, y, RGB(255, 255, 255));
      }
    }
  }
  SelectObject(hdc, old);
  DeleteDC(hdc);  
}

/***************************************************************************
...
***************************************************************************/
struct Sprite *load_gfxfile(const char *filename)
{
  png_structp pngp;
  png_infop infop;
  png_uint_32 sig_read=0;
  png_int_32 width, height, row, col;
  int bit_depth, color_type, interlace_type;
  FILE *fp;

  png_bytep *row_pointers;
   
  struct Sprite *mysprite;
  int has_mask;
  void *buf;
  BYTE *pb, *p;

  if (!(fp=fopen(filename, "rb"))) {
    MessageBox(NULL, "failed reading", filename, MB_OK);
    freelog(LOG_FATAL, "Failed reading PNG file: %s", filename);
    exit(EXIT_FAILURE);
  }
    
  if (!(pngp=png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL))) {

    freelog(LOG_FATAL, "Failed creating PNG struct");
    exit(EXIT_FAILURE);
  }
 
  if (!(infop=png_create_info_struct(pngp))) {
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
  
  has_mask=(color_type & PNG_COLOR_MASK_ALPHA);

  row_pointers=fc_malloc(sizeof(png_bytep)*height);

  for (row=0; row<height; row++)
    row_pointers[row]=fc_malloc(png_get_rowbytes(pngp, infop));

  png_read_image(pngp, row_pointers);
  png_read_end(pngp, infop);
  fclose(fp);


  mysprite = fc_malloc(sizeof(struct Sprite));
  mysprite->alphablend = FALSE;

  buf = fc_malloc((width * height) << 2);

  if (has_mask) {
    for (row = 0, pb = buf; row < height; row++) {
      for (col = 0, p = row_pointers[row]; col<width; col++) {
	*pb++ = *p++;
	*pb++ = *p++;
	*pb++ = *p++;
	if ((*p != 0) && (*p != 255)) {
	  mysprite->alphablend = TRUE;
	}
	*pb++ = *p++;
      }
    }
  } else {
    for (row = 0, pb = buf; row < height; row++) {
      for (col = 0, p = row_pointers[row]; col<width; col++) {
	*pb++ = *p++;
	*pb++ = *p++;
	*pb++ = *p++;
	*pb++ = 255;
	p++;
      }
    } 
  }

  mysprite->img.bmType = 0;
  mysprite->img.bmWidth = width;
  mysprite->img.bmHeight = height;
  mysprite->img.bmWidthBytes = width << 2;
  mysprite->img.bmPlanes = 1;
  mysprite->img.bmBitsPixel = 32;
  mysprite->img.bmBits = buf;

  if (has_mask) {
    mysprite->mask = generate_mask(mysprite->img);
  }

  mysprite->has_mask = has_mask;
  mysprite->has_fog = FALSE;
  mysprite->has_pmimg = FALSE;
  mysprite->img_cache_id = -1;
  mysprite->fog_cache_id = -1;
  mysprite->mask_cache_id = -1;
  mysprite->pmimg_cache_id = -1;
  mysprite->width = width;
  mysprite->height = height;

  for (row=0; row<height; row++)
    free(row_pointers[row]);
  free(row_pointers);
  png_destroy_read_struct(&pngp, &infop, NULL);
  return mysprite;
}

/**************************************************************************
  Create a dimmed version of this sprite's image.
**************************************************************************/
void fog_sprite(struct Sprite *sprite)
{
  int bmpsize, i;
  unsigned char *p, *pb;

  bmpsize = sprite->img.bmHeight * sprite->img.bmWidthBytes;

  /* Copy over all the bitmap info, and replace the bits pointer */
  sprite->fog = sprite->img;
  sprite->fog.bmBits = fc_malloc(bmpsize);

  /* RGBA 8888 */
  p = sprite->img.bmBits;
  pb = sprite->fog.bmBits;

#define FOG_BRIGHTNESS 65

  for (i = 0; i < bmpsize; i += 4) {
    *pb++ = *p++ * FOG_BRIGHTNESS / 100;
    *pb++ = *p++ * FOG_BRIGHTNESS / 100;
    *pb++ = *p++ * FOG_BRIGHTNESS / 100;
    *pb++ = *p++;
  }

#undef FOG_BRIGHTNESS

  sprite->has_fog = 1;
}

/**************************************************************************
  Draw a sprite to the specified device context.
**************************************************************************/
static void real_draw_sprite(struct Sprite *sprite, HDC hdc, int x, int y,
			     int w, int h, int offset_x, int offset_y,
			     bool fog)
{
  HDC hdccomp;
  HBITMAP tempbit;
  HBITMAP bmpimg;
  bool blend;

  if (!sprite) return;

  w = MIN(w, sprite->width - offset_x);
  h = MIN(h, sprite->height - offset_y);

  blend = enable_alpha && sprite->alphablend;

  if (!fog && blend && !sprite->has_pmimg) {
    sprite->pmimg = premultiply_alpha(sprite->img);
    sprite->has_pmimg = TRUE;
  }

  if (!fog && blend && !have_AlphaBlend) {
    /* Software alpha blending.  Even works in 16-bit.
     * FIXME: make this less slow. */
    int row, col;
    for (row = 0; row < h; row++) {
      unsigned char *src = (unsigned char *)sprite->pmimg.bmBits 
			   + sprite->pmimg.bmWidthBytes * row;
      for (col = 0; col < w; col++) {
	COLORREF cr;
	unsigned char r, g, b;

	if (src[3] == 255) {
	  SetPixel(hdc, col + x, row + y, RGB(src[0], src[1], src[2]));
	} else if (src[3] != 0) {
	  cr = GetPixel(hdc, col + x, row + y);

	  r = (src[0] + GetRValue(cr) * (256 - src[3])) >> 8;
	  g = (src[1] + GetGValue(cr) * (256 - src[3])) >> 8;
	  b = (src[2] + GetBValue(cr) * (256 - src[3])) >> 8;

	  SetPixel(hdc, col + x, row + y, RGB(r, g, b));
	}
	src += 4;
      } 
    }
    return;
  }

  hdccomp = CreateCompatibleDC(hdc);
  
  if (fog) {
    bmpimg = getcachehbitmap(&(sprite->fog), &(sprite->fog_cache_id));
  } else if (blend) {
    bmpimg = getcachehbitmap(&(sprite->pmimg), &(sprite->pmimg_cache_id));
  } else {
    bmpimg = getcachehbitmap(&(sprite->img), &(sprite->img_cache_id));
  }

  tempbit = SelectObject(hdccomp, bmpimg);

  if (blend) {
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    AlphaBlend(hdc, x, y, w, h, hdccomp, offset_x, offset_y, w, h, bf);
  } else {
    if (sprite->has_mask) {
      HDC hdcmask;
      HBITMAP tempmask;
      HBITMAP bmpmask;

      bmpmask = getcachehbitmap(&(sprite->mask), &(sprite->mask_cache_id));

      hdcmask = CreateCompatibleDC(hdc);

      tempmask = SelectObject(hdcmask, bmpmask);

      BitBlt(hdc, x, y, w, h, hdccomp, offset_x, offset_y, SRCINVERT);
      BitBlt(hdc, x, y, w, h, hdcmask, offset_x, offset_y, SRCAND);
      BitBlt(hdc, x, y, w, h, hdccomp, offset_x, offset_y, SRCINVERT);

      SelectObject(hdcmask, tempmask);
      DeleteDC(hdcmask);
    } else {
      BitBlt(hdc, x, y, w, h, hdccomp, offset_x, offset_y, SRCCOPY);
    }
  }

  SelectObject(hdccomp, tempbit);
  DeleteDC(hdccomp);
}

/**************************************************************************
  Wrapper for real_draw_sprite(), for fogged sprites.
**************************************************************************/
void draw_sprite_fog(struct Sprite *sprite, HDC hdc, int x, int y)
{
  real_draw_sprite(sprite, hdc, x, y, sprite->width, sprite->height, 0, 0,
		   TRUE);
}

/**************************************************************************
  Wrapper for real_draw_sprite(), for unfogged sprites.
**************************************************************************/
void draw_sprite(struct Sprite *sprite, HDC hdc, int x, int y)
{
  real_draw_sprite(sprite, hdc, x, y, sprite->width, sprite->height, 0, 0,
		   FALSE);
}

/**************************************************************************
  Wrapper for real_draw_sprite(), for partial sprites.
**************************************************************************/
void draw_sprite_part(struct Sprite *sprite, HDC hdc, int x, int y, int w,
		      int h, int offset_x, int offset_y)
{
  real_draw_sprite(sprite, hdc, x, y, w, h, offset_x, offset_y, FALSE);
}

/**************************************************************************
  Draw stippled fog, using the mask of the specified sprite.
**************************************************************************/
void draw_fog(struct Sprite *sprmask, HDC hdc, int x, int y)
{
  HDC hdcmask;
  HDC hdcsrc;

  HBITMAP tempmask;
  HBITMAP tempsrc;

  HBITMAP bmpmask;

  int w, h;

  w = sprmask->width;
  h = sprmask->height;

  bmpmask = getcachehbitmap(&(sprmask->mask), &(sprmask->mask_cache_id));

  hdcsrc = CreateCompatibleDC(hdc);
  tempsrc = SelectObject(hdcsrc, bmpmask);

  hdcmask = CreateCompatibleDC(hdc);
  tempmask = SelectObject(hdcmask, fogmask); 

  BitBlt(hdcmask, 0, 0, w, h, hdcsrc, 0, 0, SRCCOPY);
  
  SelectObject(hdcsrc, stipple);

  BitBlt(hdcmask, 0, 0, w, h, hdcsrc, 0, 0, SRCPAINT);

  SelectObject(hdcsrc, tempsrc);
  DeleteDC(hdcsrc);

  BitBlt(hdc, x, y, w, h, hdcmask, 0, 0, SRCAND);

  SelectObject(hdcmask, tempmask);
  DeleteDC(hdcmask);
}
        
/**************************************************************************
  Free memory used by this sprite.
**************************************************************************/
void free_sprite(struct Sprite *s)
{
  if (s->has_mask) {
    free(s->mask.bmBits);
  }

  if (s->has_fog) {
    free(s->fog.bmBits);
  }

  if (s->has_pmimg) {
    free(s->pmimg.bmBits);
  }

  free(s->img.bmBits);

  free(s);
  if (bitmapcache) {
    DeleteObject(bitmapcache);
    bitmapcache=NULL;
  }
  sprcache = NULL;
}

/**************************************************************************

**************************************************************************/
bool isometric_view_supported(void)
{
  return TRUE;
}

/**************************************************************************

**************************************************************************/
bool overhead_view_supported(void)
{
  return TRUE;
}
