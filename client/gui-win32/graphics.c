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

#include "drop_cursor.xbm"
#include "drop_cursor_mask.xbm"
#include "goto_cursor.xbm"
#include "goto_cursor_mask.xbm"
#include "nuke_cursor.xbm"
#include "nuke_cursor_mask.xbm"
#include "patrol_cursor.xbm"
#include "patrol_cursor_mask.xbm"

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

HCURSOR goto_cursor;
HCURSOR drop_cursor;
HCURSOR nuke_cursor;
HCURSOR patrol_cursor;

static void scale_cursor(int old_w, int old_h, int new_w, int new_h,
			 char *old_bits, char *new_bits, bool flip);

extern BOOL have_AlphaBlend;

extern BOOL (WINAPI * AlphaBlend)(HDC,int,int,int,int,HDC,int,int,int,int,BLENDFUNCTION);

/**************************************************************************

**************************************************************************/
void
load_intro_gfx(void)
{
  intro_gfx_sprite=load_gfxfile(main_intro_filename);
  radar_gfx_sprite = load_gfxfile(minimap_intro_filename);     
}

/**************************************************************************
  Scales and converts an .xbm cursor so win32 can use it.
**************************************************************************/
static void scale_cursor(int old_w, int old_h, int new_w, int new_h,
			 char *old_bits, char *new_bits, bool flip)
{
  int x, y;
  int bytew;

  bytew = 1;
  while(bytew < old_w) {
    bytew <<= 1;
  }
  for (x = 0; x < new_w * new_h / 8; x++) {
    new_bits[x] = 0;
  }

  for (y = 0; y < old_h; y++) {
    for (x = 0; x < old_w; x++) {
      int old_byte_pos, old_bit_pos;
      int new_byte_pos, new_bit_pos;

      old_byte_pos = (x + y * bytew) / 8;
      old_bit_pos  = (x + y * bytew) % 8;
      new_byte_pos = (x + y * new_w) / 8;
      new_bit_pos  = (x + y * new_w) % 8;

      if (old_bits[old_byte_pos] & (1 << old_bit_pos)) {
	new_bits[new_byte_pos] |= (128 >> new_bit_pos);
      }
    }
  }
  if (flip) {
    for (x = 0; x < new_w * new_h / 8; x++) {
      new_bits[x] = ~new_bits[x];
    }
  }
}

/**************************************************************************

**************************************************************************/
void
load_cursors(void)
{
  /* For some reason win32 lets you enter a cursor size, which
   * only works as long as it's this size. */
  int width = GetSystemMetrics(SM_CXCURSOR);
  int height = GetSystemMetrics(SM_CYCURSOR);

  char *new_bits = fc_malloc(width * height / 8);
  char *new_mask_bits = fc_malloc(width * height / 8);

  scale_cursor(goto_cursor_width, goto_cursor_height,
	       width, height,
	       goto_cursor_mask_bits, new_mask_bits, TRUE);
  scale_cursor(goto_cursor_width, goto_cursor_height,
	       width, height,
	       goto_cursor_bits, new_bits, FALSE);

  goto_cursor = CreateCursor(freecivhinst,
			     goto_cursor_x_hot, goto_cursor_y_hot,
			     width, height,
			     new_mask_bits,
			     new_bits);

  scale_cursor(drop_cursor_width, drop_cursor_height,
	       width, height,
	       drop_cursor_mask_bits, new_mask_bits, TRUE);
  scale_cursor(drop_cursor_width, drop_cursor_height,
	       width, height,
	       drop_cursor_bits, new_bits, FALSE);

  drop_cursor = CreateCursor(freecivhinst,
			     drop_cursor_x_hot, drop_cursor_y_hot,
			     width, height,
			     new_mask_bits,
			     new_bits);

  scale_cursor(nuke_cursor_width, nuke_cursor_height,
	       width, height,
	       nuke_cursor_mask_bits, new_mask_bits, TRUE);
  scale_cursor(nuke_cursor_width, nuke_cursor_height,
	       width, height,
	       nuke_cursor_bits, new_bits, FALSE);

  nuke_cursor = CreateCursor(freecivhinst,
			     nuke_cursor_x_hot, nuke_cursor_y_hot,
			     width, height,
			     new_mask_bits,
			     new_bits);

  scale_cursor(patrol_cursor_width, patrol_cursor_height,
	       width, height,
	       patrol_cursor_mask_bits, new_mask_bits, TRUE);
  scale_cursor(patrol_cursor_width, patrol_cursor_height,
	       width, height,
	       patrol_cursor_bits, new_bits, FALSE);

  patrol_cursor = CreateCursor(freecivhinst,
			       patrol_cursor_x_hot, patrol_cursor_y_hot,
			       width, height,
			       new_mask_bits,
			       new_bits);

  free(new_bits);
  free(new_mask_bits);
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

 *************************************************************************/
HBITMAP BITMAP2HBITMAP(BITMAP *bmp)
{
  HDC hdc;
  BITMAPINFO bi;
  HBITMAP hbmp;

  bi.bmiHeader.biSize           = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth          = bmp->bmWidth;
  bi.bmiHeader.biHeight         = -bmp->bmHeight;
  bi.bmiHeader.biPlanes         = bmp->bmPlanes;
  bi.bmiHeader.biBitCount       = bmp->bmBitsPixel;

  bi.bmiHeader.biCompression    = BI_RGB;
  bi.bmiHeader.biSizeImage      = 0;
  bi.bmiHeader.biXPelsPerMeter  = 0;
  bi.bmiHeader.biYPelsPerMeter  = 0;
  bi.bmiHeader.biClrUsed        = 0;
  bi.bmiHeader.biClrImportant   = 0;

  hdc = GetDC(root_window);
  hbmp = CreateDIBitmap(hdc, &bi.bmiHeader, CBM_INIT, bmp->bmBits, &bi, 0);
  ReleaseDC(root_window, hdc);

  return hbmp;
}


/**************************************************************************

**************************************************************************/
#if 0
static void HBITMAP2BITMAP(HBITMAP hbmp,BITMAP *bmp)
{
  int bmpsize;
  GetObject(hbmp,sizeof(BITMAP),bmp);
  bmpsize=bmp->bmHeight*bmp->bmWidthBytes;
  bmp->bmBits=fc_malloc(bmpsize);
  GetBitmapBits(hbmp,bmpsize,bmp->bmBits);
}
#endif

/**************************************************************************
  Retrieve an image from the HBITMAP cache, or if it is not there, place
  it into the cache and return it.
**************************************************************************/
static HBITMAP getcachehbitmap(BITMAP *bmp, int *cache_id)
{
  struct Bitmap_cache *bmpc;
  bmpc = &bitmap_cache[*cache_id];
  if (bmpc->src != bmp) {
    unsigned char *src, *dst;
    BITMAP alphabmp;

    int row, col;
    cache_id_count++;
    cache_id_count %= CACHE_SIZE;
    *cache_id = cache_id_count;
    bmpc = &bitmap_cache[cache_id_count];
    DeleteObject(bmpc->hbmp);
    bmpc->src = bmp;

    if (have_AlphaBlend) {
      alphabmp = *bmp;
      alphabmp.bmBits = fc_malloc(bmp->bmWidthBytes * bmp->bmHeight);

      for (row = 0; row < bmp->bmHeight; row++) {
	src = (unsigned char *)bmp->bmBits + bmp->bmWidthBytes * row;
	dst = (unsigned char *)alphabmp.bmBits + alphabmp.bmWidthBytes * row;
	for (col = 0; col < bmp->bmWidth; col++) {
	  dst[0] = src[0] * src[3] / 255;
	  dst[1] = src[1] * src[3] / 255;
	  dst[2] = src[2] * src[3] / 255;
	  dst[3] = src[3];
	  src += 4;
	  dst += 4;
	}
      }
    
      bmpc->hbmp = BITMAP2HBITMAP(&alphabmp);
      free(alphabmp.bmBits);
    } else {
      bmpc->hbmp = BITMAP2HBITMAP(bmp);
    }
  }
  return bmpc->hbmp;
}


/**************************************************************************
  Generate a mask from a bitmap's alpha channel, using a different dither
  function depending on transparency level.
**************************************************************************/
static HBITMAP generate_mask(BITMAP *bmp)
{
  BITMAP mask;
  HBITMAP hbmp;
  unsigned char *src, *dst;
  int row, col;

  mask = *bmp;
  mask.bmBits = fc_malloc(mask.bmWidthBytes * mask.bmHeight);

  for (row = 0; row < bmp->bmHeight; row++) {
    src = (unsigned char *)bmp->bmBits + bmp->bmWidthBytes * row;
    dst = (unsigned char *)mask.bmBits + mask.bmWidthBytes * row;
    for (col = 0; col < bmp->bmWidth; col++) {
      src += 3;
      if (*src > 255 * 4 / 5) {
	*dst++ = 0;
	*dst++ = 0;
	*dst++ = 0;
      } else if (*src > 255 * 3 / 5) {
	if ((row + col * 2) % 4 == 0) {
	  *dst++ = 255;
	  *dst++ = 255;
	  *dst++ = 255;
	} else {
	  *dst++ = 0;
	  *dst++ = 0;
	  *dst++ = 0;
	}
      } else if (*src > 255 * 2 / 5) {
	if ((row + col) % 2 == 0) {
	  *dst++ = 255;
	  *dst++ = 255;
	  *dst++ = 255;
	} else {
	  *dst++ = 0;
	  *dst++ = 0;
	  *dst++ = 0;
	}
      } else if (*src > 255 * 1 / 5) {
	if ((row + col * 2) % 4 == 0) {
	  *dst++ = 0;
	  *dst++ = 0;
	  *dst++ = 0;
	} else {
	  *dst++ = 255;
	  *dst++ = 255;
	  *dst++ = 255;
	}
      } else {
	*dst++ = 255;
	*dst++ = 255;
	*dst++ = 255;
      }
      *dst++ = 0;
      src++;
    }
  }
  hbmp = BITMAP2HBITMAP(&mask);
  free(mask.bmBits);
  return hbmp;
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
  mysprite->img_cache_id = 0;
  mysprite->fog_cache_id = 0;
  mysprite->width = width;
  mysprite->height = height;
  mysprite->has_mask = 0;
  mysprite->has_fog = 0;

  mysprite->img.bmType = 0;
  mysprite->img.bmWidth = width;
  mysprite->img.bmHeight = height;
  mysprite->img.bmWidthBytes = width << 2;
  mysprite->img.bmPlanes = 1;
  mysprite->img.bmBitsPixel = 32;
  mysprite->img.bmBits = fc_malloc(width * height * 4);

  if ((sprmask && sprmask->has_mask) || sprsrc->has_mask) {
    mysprite->has_mask = 1;
  }

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

  buf = fc_malloc((width * height) << 2);

  if (has_mask) {
    for (row = 0, pb = buf; row < height; row++) {
      for (col = 0, p = row_pointers[row]; col<width; col++) {
	*pb++ = *p++;
	*pb++ = *p++;
	*pb++ = *p++;
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

  mysprite->has_mask = has_mask;
  mysprite->has_fog = 0;
  mysprite->img_cache_id = 0;
  mysprite->fog_cache_id = 0;
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
			     bool fog)
{
  HDC hdccomp;
  HBITMAP tempbit;
  HBITMAP bmpimg;
  int w, h;

  if (!sprite) return;

  w = sprite->width;
  h = sprite->height;

  hdccomp = CreateCompatibleDC(hdc);
  
  if (fog) {
    bmpimg = getcachehbitmap(&(sprite->fog), &(sprite->fog_cache_id));
  } else {
    bmpimg = getcachehbitmap(&(sprite->img), &(sprite->img_cache_id));
  }

  tempbit = SelectObject(hdccomp, bmpimg);

  if (have_AlphaBlend) {
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    AlphaBlend(hdc, x, y, w, h, hdccomp, 0, 0, w, h, bf);
  } else {
    if (sprite->has_mask) {
      HDC hdcmask;
      HBITMAP tempmask;
      HBITMAP bmpmask;

      bmpmask = generate_mask(&sprite->img);

      hdcmask = CreateCompatibleDC(hdc);

      tempmask = SelectObject(hdcmask, bmpmask);

      BitBlt(hdc, x, y, w, h, hdccomp, 0, 0, SRCINVERT);
      BitBlt(hdc, x, y, w, h, hdcmask, 0, 0, SRCAND);
      BitBlt(hdc, x, y, w, h, hdccomp, 0, 0, SRCINVERT);

      SelectObject(hdcmask, tempmask);
      DeleteDC(hdcmask);
      DeleteObject(bmpmask);
    } else {
      BitBlt(hdc, x, y, w, h, hdccomp, 0, 0, SRCCOPY);
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
  real_draw_sprite(sprite, hdc, x, y, TRUE);
}

/**************************************************************************
  Wrapper for real_draw_sprite(), for unfogged sprites.
**************************************************************************/
void draw_sprite(struct Sprite *sprite, HDC hdc, int x, int y)
{
  real_draw_sprite(sprite, hdc, x, y, FALSE);
}

/**************************************************************************
  Draw stippled fog, using the mask of the specified sprite.
**************************************************************************/
void draw_fog(struct Sprite *sprmask, HDC hdc, int x, int y)
{
#if 0
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
#endif
}
        
/**************************************************************************
  Free memory used by this sprite.
**************************************************************************/
void free_sprite(struct Sprite *s)
{
  if (s->has_mask) {
  }

  if (s->has_fog) {
    free(s->fog.bmBits);
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
