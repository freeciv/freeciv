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
  return CreateBitmap(bmp->bmWidth,bmp->bmHeight,bmp->bmPlanes,
                      bmp->bmBitsPixel,bmp->bmBits);
}


/**************************************************************************

**************************************************************************/
static void HBITMAP2BITMAP(HBITMAP hbmp,BITMAP *bmp)
{
  int bmpsize;
  GetObject(hbmp,sizeof(BITMAP),bmp);
  bmpsize=bmp->bmHeight*bmp->bmWidthBytes;
  bmp->bmBits=fc_malloc(bmpsize);
  GetBitmapBits(hbmp,bmpsize,bmp->bmBits);
}

/**************************************************************************
  Retrieve an image from the HBITMAP cache, or if it is not there, place
  it into the cache and return it.
**************************************************************************/
static HBITMAP getcachehbitmap(BITMAP *bmp, int *cache_id)
{
  struct Bitmap_cache *bmpc;
  bmpc = &bitmap_cache[*cache_id];
  if (bmpc->src != bmp) {
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
  HDC hdc;
  HDC hdcsrc;
  HDC hdcdst;
  HBITMAP dstbmp;
  HBITMAP dstmask = NULL;
  HBITMAP srcbmp;
  HBITMAP srcmask;
  HBITMAP srcsave;
  HBITMAP dstsave;

  hdc = GetDC(root_window);

  hdcsrc = CreateCompatibleDC(hdc);
  hdcdst = CreateCompatibleDC(hdc);

  if (sprcache != sprsrc) {
    if (bitmapcache) {
      DeleteObject(bitmapcache);
    }
    sprcache = sprsrc;
    bitmapcache = BITMAP2HBITMAP(&(sprsrc->img));
  }
  srcbmp = bitmapcache;
  dstbmp = CreateCompatibleBitmap(hdc, width, height);
  
  srcsave = SelectObject(hdcsrc, srcbmp);
  dstsave = SelectObject(hdcdst, dstbmp);
  BitBlt(hdcdst, 0, 0, width, height, hdcsrc, x, y, SRCCOPY);

  if (sprsrc->has_mask || (sprmask && sprmask->has_mask)) {
    dstmask = CreateBitmap(width, height, 1, 1, NULL);
    SelectObject(hdcdst, dstmask);
  }

  if (sprsrc->has_mask) {
    srcmask = BITMAP2HBITMAP(&sprsrc->mask);
    SelectObject(hdcsrc, srcmask);
    BitBlt(hdcdst, 0, 0, width, height, hdcsrc, x, y, SRCCOPY);
    SelectObject(hdcsrc, srcbmp);
    DeleteObject(srcmask);
  }

  if (sprmask && sprmask->has_mask) {
    srcmask = BITMAP2HBITMAP(&sprmask->mask);
    SelectObject(hdcsrc, srcmask);
    BitBlt(hdcdst, 0, 0, width, height, hdcsrc,
	   x - mask_offset_x, y - mask_offset_y, SRCPAINT);    
    SelectObject(hdcsrc, srcbmp);
    DeleteObject(srcmask);
  }

  mysprite = fc_malloc(sizeof(struct Sprite));
  mysprite->img_cache_id = 0;
  mysprite->fog_cache_id = 0;
  mysprite->mask_cache_id = 0;
  mysprite->width = width;
  mysprite->height = height;
  HBITMAP2BITMAP(dstbmp, &mysprite->img);
  mysprite->has_mask = 0;
  if (dstmask) {
    mysprite->has_mask = 1;
    HBITMAP2BITMAP(dstmask, &mysprite->mask);
  }
  mysprite->has_fog = 0;

  SelectObject(hdcsrc, srcsave);
  SelectObject(hdcdst, dstsave);
  ReleaseDC(root_window, hdc);
  DeleteDC(hdcsrc);
  DeleteDC(hdcdst);
  if (dstmask) {
    DeleteObject(dstmask);
  }
  DeleteObject(dstbmp);

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
  BITMAPINFO bi;
  void *buf;
  BYTE *pb, *p;
  HDC hdc;
  HBITMAP bmp;
  HBITMAP dib;

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
  png_set_invert_alpha(pngp); 

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


  mysprite=fc_malloc(sizeof(struct Sprite));

  /* init DIB BITMAPINFOHEADER */
  bi.bmiHeader.biSize           = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth          = width;
  bi.bmiHeader.biHeight         = height;
  bi.bmiHeader.biPlanes         = 1;
  bi.bmiHeader.biBitCount       = 32;

  bi.bmiHeader.biCompression    = BI_RGB;
  bi.bmiHeader.biSizeImage      = 0;
  bi.bmiHeader.biXPelsPerMeter  = 0;
  bi.bmiHeader.biYPelsPerMeter  = 0;
  bi.bmiHeader.biClrUsed        = 0;
  bi.bmiHeader.biClrImportant   = 0;

  hdc=GetDC(root_window);
  dib=CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &buf, NULL, 0);
  bmp=CreateCompatibleBitmap(hdc,bi.bmiHeader.biWidth, bi.bmiHeader.biHeight);


  for (row = height-1, pb = buf; row>=0; row--) {
    for (col = 0, p = row_pointers[row]; col<width; col++) {
      *pb++=*p++;
      *pb++=*p++;
      *pb++=*p++;
      *pb++=0;
      p++;
    } 
  }
  
  SetDIBits(hdc, bmp, 0, bi.bmiHeader.biHeight,
	    buf, &bi, DIB_RGB_COLORS);
  DeleteObject(dib);
  ReleaseDC(root_window, hdc);
  HBITMAP2BITMAP(bmp, &mysprite->img);
  DeleteObject(bmp);
  if (has_mask) {
    hdc=CreateCompatibleDC(NULL);
    dib=CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &buf, NULL, 0);
    for (row = height-1, pb = buf; row >= 0; row--) {
      for (col=0, p=row_pointers[row]; col<width; col++) {
        p+=3;
        *pb++=*p;
        *pb++=*p;
        *pb++=*p;
        *pb++=0;
        p+=1;
      }
    }
    bmp=CreateCompatibleBitmap(hdc, bi.bmiHeader.biWidth,
			       bi.bmiHeader.biHeight);
    SetDIBits(hdc, bmp, 0, bi.bmiHeader.biHeight, buf, 
	      &bi, DIB_RGB_COLORS);
    DeleteDC(hdc);
    HBITMAP2BITMAP(bmp,&mysprite->mask);
    DeleteObject(dib);
    DeleteObject(bmp);
  }

  mysprite->has_mask = has_mask;
  mysprite->has_fog = 0;
  mysprite->img_cache_id = 0;
  mysprite->fog_cache_id = 0;
  mysprite->mask_cache_id = 0;
  mysprite->width = width;
  mysprite->height = height;


  for (row=0; row<height; row++)
    free(row_pointers[row]);
  free(row_pointers);
  png_destroy_read_struct(&pngp, &infop, NULL);
  return mysprite;
}

/**************************************************************************
  Create a half bright version of this sprite's image.  Doesn't work in
  paletted video modes.
**************************************************************************/
void fog_sprite(struct Sprite *sprite)
{
  int bmpsize, i;

  if (sprite->img.bmBitsPixel != 32 && sprite->img.bmBitsPixel != 24
      && sprite->img.bmBitsPixel != 15 && sprite->img.bmBitsPixel != 16) {
    return;
  }

  bmpsize = sprite->img.bmHeight * sprite->img.bmWidthBytes;

  /* Copy over all the bitmap info, and replace the bits pointer */
  sprite->fog = sprite->img;
  sprite->fog.bmBits = fc_malloc(bmpsize);

#define FOG_BRIGHTNESS 65

  /* RGBA 8888 */
  if (sprite->img.bmBitsPixel == 32) {
    unsigned int *src, *dst;
    unsigned int r, g, b, a;

    src = sprite->img.bmBits;
    dst = sprite->fog.bmBits;

    for (i = 0; i < bmpsize; i += 4) {
      a =  (*src & 0xFF000000) >> 24;
      r = ((*src & 0x00FF0000) >> 16) * FOG_BRIGHTNESS / 100;
      g = ((*src & 0x0000FF00) >> 8 ) * FOG_BRIGHTNESS / 100;
      b = ((*src & 0x000000FF)      ) * FOG_BRIGHTNESS / 100;
      *dst++ = (a << 24) | (r << 16) | (g << 8) | b;
      src++;
    }
  /* RGB 888 */
  } else if (sprite->img.bmBitsPixel == 24) {
    unsigned char *src, *dst;

    src = sprite->img.bmBits;
    dst = sprite->fog.bmBits;

    for (i = 0; i < bmpsize; i++) {
      *dst++ = *src++ * FOG_BRIGHTNESS / 100;
    }
  /* RGB 565 */
  } else if (sprite->img.bmBitsPixel == 16) {
    unsigned short *src, *dst;
    unsigned int r, g, b;

    src = sprite->img.bmBits;
    dst = sprite->fog.bmBits;

    for (i = 0; i < bmpsize; i += 2) {
      r = ((*src & 0xF800) >> 11) * FOG_BRIGHTNESS / 100;
      g = ((*src & 0x07E0) >> 5 ) * FOG_BRIGHTNESS / 100;
      b = ((*src & 0x001F)      ) * FOG_BRIGHTNESS / 100;
      *dst++ = (r << 11) | (g << 5) | b;
      src++;
    }
  /* RGB 555 */
  } else if (sprite->img.bmBitsPixel == 15) {
    unsigned short *src, *dst;
    unsigned int r, g, b;

    src = sprite->img.bmBits;
    dst = sprite->fog.bmBits;

    for (i = 0; i < bmpsize; i += 2) {
      r = ((*src & 0x7C00) >> 10) * FOG_BRIGHTNESS / 100;
      g = ((*src & 0x03E0) >> 5 ) * FOG_BRIGHTNESS / 100;
      b = ((*src & 0x001F)      ) * FOG_BRIGHTNESS / 100;
      *dst++ = (r << 10) | (g << 5) | b;
      src++;
    }
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

  if (sprite->has_mask) {
    HDC hdcmask;
    HBITMAP tempmask;
    HBITMAP bmpmask;

    bmpmask = getcachehbitmap(&(sprite->mask), &(sprite->mask_cache_id));

    hdcmask = CreateCompatibleDC(hdc);

    tempmask = SelectObject(hdcmask, bmpmask);

    BitBlt(hdc, x, y, w, h, hdccomp, 0, 0, SRCINVERT);
    BitBlt(hdc, x, y, w, h, hdcmask, 0, 0, SRCAND);
    BitBlt(hdc, x, y, w, h, hdccomp, 0, 0, SRCINVERT);

    SelectObject(hdcmask, tempmask);
    DeleteDC(hdcmask);
  } else {
    BitBlt(hdc, x, y, w, h, hdccomp, 0, 0, SRCCOPY);
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
