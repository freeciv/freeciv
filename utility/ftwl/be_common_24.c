/**********************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Project
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

#include <assert.h>
#include <stdio.h>
#include <png.h>

#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "support.h"

#include "back_end.h"
#include "be_common_24.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#define ENABLE_TRANSPARENCY	1
#define DO_CYCLE_MEASUREMENTS	0

/* Blend the RGB values of two pixel 3-byte pixel arrays
 * based on a source alpha value. */
#define ALPHA_BLEND(d, A, s)          \
do {                                  \
  d[0] = (((s[0]-d[0])*(A))>>8)+d[0]; \
  d[1] = (((s[1]-d[1])*(A))>>8)+d[1]; \
  d[2] = (((s[2]-d[2])*(A))>>8)+d[2]; \
} while(0)

/*************************************************************************
  Combine RGB colour values into a 24bpp colour value.
*************************************************************************/
be_color be_get_color(int red, int green, int blue, int alpha)
{
  assert(red >= 0 && red <= 255);
  assert(green >= 0 && green <= 255);
  assert(blue >= 0 && blue <= 255);
  assert(alpha >= 0 && alpha <= 255);

  return red << 24 | green << 16 | blue << 8 | alpha;
}

/*************************************************************************
  Set a pixel in a given buffer (dest) to a given color (color).
*************************************************************************/
static void set_color(be_color color, unsigned char *dest)
{
  dest[0] = ((color >> 24) & 0xff);
  dest[1] = ((color >> 16) & 0xff);
  dest[2] = ((color >> 8) & 0xff);
  dest[3] = ((color) & 0xff);
}

/*************************************************************************
  Image blit with transparency.  Alpha value lifted from src.
  MIN_OPACITY means the pixel should not be drawn.  MAX_OPACITY means
  the pixels should not be blended.
*************************************************************************/
/* non static to prevent inlining */
void image_blit_masked_trans(const struct ct_size *size,
			     const struct image *src,
			     const struct ct_point *src_pos,
			     struct image *dest,
			     const struct ct_point *dest_pos);
void image_blit_masked_trans(const struct ct_size *size,
			     const struct image *src,
			     const struct ct_point *src_pos,
			     struct image *dest,
			     const struct ct_point *dest_pos)
{
  int x, y;

  unsigned char *psrc = IMAGE_GET_ADDRESS(src, src_pos->x,
					  src_pos->y);
  unsigned char *pdest = IMAGE_GET_ADDRESS(dest, dest_pos->x,
					   dest_pos->y);
  int w = size->width;
  int h = size->height;
  int extra_src = (src->width - size->width) * 4;
  int extra_dest = (dest->width - size->width) * 4;

  //printf("BLITTING %dx%d",size->width,size->height);

  for (y = h; y > 0; y--) {
    for (x = w; x > 0; x--) {
      unsigned char mask_value = psrc[3];

#if ENABLE_TRANSPARENCY
      if(mask_value == MAX_OPACITY) {
	memcpy(pdest, psrc, 4);
      } else if(mask_value == MIN_OPACITY) {
	/* 
	 * Empty since we never copy MAX_OPACITY pixels - that is why
	 * they exist! 
	 */
      } else {
	/* We need to perform transparency */
	ALPHA_BLEND(pdest, psrc[3], psrc);
      }
#else
      if (mask_value != MIN_OPACITY) {
	memcpy(pdest, psrc, 4);
      }
#endif
      psrc += 4;
      pdest += 4;
    }
    psrc += extra_src;
    pdest += extra_dest;
  }
}

/*************************************************************************
  ...
*************************************************************************/
static void clip_two_regions(const struct image *dest,
			     const struct image *src,
			     struct ct_size *size,
			     struct ct_point *dest_pos,
			     struct ct_point *src_pos)
{
  int dx, dy;
  struct ct_point real_src_pos, real_dest_pos;
  struct ct_size real_size;

  struct ct_rect src_use =
      { src_pos->x, src_pos->y, size->width, size->height };
  struct ct_rect dest_use =
      { dest_pos->x, dest_pos->y, size->width, size->height };

  real_src_pos = *src_pos;
  real_dest_pos = *dest_pos;

  ct_clip_rect(&src_use, &src->full_rect);
  ct_clip_rect(&dest_use, &dest->full_rect);

  real_size.width = MIN(src_use.width, dest_use.width);
  real_size.height = MIN(src_use.height, dest_use.height);

  dx = MAX(src_use.x - src_pos->x, dest_use.x - dest_pos->x);
  dy = MAX(src_use.y - src_pos->y, dest_use.y - dest_pos->y);

  real_src_pos.x += dx;
  real_src_pos.y += dy;

  real_dest_pos.x += dx;
  real_dest_pos.y += dy;

  *size=real_size;
  *src_pos=real_src_pos;
  *dest_pos=real_dest_pos;
}

/*************************************************************************
  Blit one image onto another, using transparency value given on all
  pixels that have alpha mask set.
*************************************************************************/
void image_copy(struct image *dest, struct image *src,
                const struct ct_size *size, const struct ct_point *dest_pos,
                const struct ct_point *src_pos)
{
  struct ct_point real_src_pos = *src_pos, real_dest_pos = *dest_pos;
  struct ct_size real_size = *size;

  clip_two_regions(dest, src, &real_size, &real_dest_pos, &real_src_pos);

  if (DO_CYCLE_MEASUREMENTS) {
#define rdtscll(val) __asm__ __volatile__ ("rdtsc" : "=A" (val))
    unsigned long long start, end;
    static unsigned long long total_clocks = 0, total_pixels = 0;
    static int total_blits = 0;

    rdtscll(start);

    image_blit_masked_trans(&real_size, src, &real_src_pos, dest,
			    &real_dest_pos);

    rdtscll(end);
    printf("BLITTING %dx%d %dx%d->%dx%d %lld %d\n", real_size.width,
	   real_size.height, src->width, src->height, dest->width, dest->height,
	   end - start, real_size.width * real_size.height);
    total_clocks += (end - start);
    total_pixels += (real_size.width * real_size.height);
    total_blits++;
    if ((total_blits % 1000) == 0) {
      printf("%f cycles per pixel\n", (float) total_clocks / total_pixels);
    }
  } else {
    image_blit_masked_trans(&real_size, src, &real_src_pos, dest,
			    &real_dest_pos);
  }
}

/*************************************************************************
  See be_draw_bitmap() below.
*************************************************************************/
void draw_mono_bitmap(struct image *image, be_color color,
                      const struct ct_point *position, 
                      struct FT_Bitmap_ *bitmap)
{
  int x, y;
  unsigned char *pbitmap = (unsigned char *) bitmap->buffer;
  unsigned char tmp[4];
  struct ct_point pos;

  set_color(color, tmp);

  for (y = 0; y < bitmap->rows; y++) {
    pos.y = y + position->y;
    for (x = 0; x < bitmap->width; x++) {
      pos.x = x + position->x;
      if (ct_point_in_rect(&pos, &image->full_rect)) {

	unsigned char bv = bitmap->buffer[x / 8 + bitmap->pitch * y];
	if (TEST_BIT(bv, 7 - (x % 8))) {
	  unsigned char *p =
	      IMAGE_GET_ADDRESS(image, x + position->x, y + position->y);

	  IMAGE_CHECK(image, x + position->x, y + position->y);
	  memcpy(p, tmp, 4);
	}
      }
    }
    pbitmap += bitmap->pitch;
  }
}

/*************************************************************************
  Draw the given bitmap (ie a 1bpp pixmap) in the given color and 
  position.
*************************************************************************/
void draw_alpha_bitmap(struct image *image, be_color color_,
                       const struct ct_point *position,
                       struct FT_Bitmap_ *bitmap)
{
  int x, y;
  unsigned char color[4];
  unsigned char *pbitmap = (unsigned char *) bitmap->buffer;
  struct ct_point pos;

  set_color(color_, color);

  for (y = 0; y < bitmap->rows; y++) {
    pos.y = y + position->y;
    for (x = 0; x < bitmap->width; x++) {
      pos.x = x + position->x;
      if (ct_point_in_rect(&pos, &image->full_rect)) {
	unsigned short transparency = pbitmap[x];
	unsigned char tmp[4];
	unsigned char *p =
	    IMAGE_GET_ADDRESS(image, position->x + x, position->y + y);

	IMAGE_CHECK(image, x, y);
	ALPHA_BLEND(tmp, transparency, p);
	memcpy(p, tmp, 4);
      }
    }
    pbitmap += bitmap->pitch;
  }
}

/*************************************************************************
  Set the alpha mask of pixels in a given region of an image.
*************************************************************************/
void image_set_alpha(const struct image *image, const struct ct_rect *rect,
		     unsigned char alpha)
{
  int x, y;

  for (y = rect->y; y < rect->y + rect->height; y++) {
    for (x = rect->x; x < rect->x + rect->width; x++) {
      IMAGE_CHECK(image, x, y);
      IMAGE_GET_ADDRESS(image, x, y)[3] = alpha;
    }
  }
}

/*************************************************************************
  Perform 
     dest_alpha = (dest_alpha * src_alpha)/256
*************************************************************************/
void image_multiply_alphas(struct image *dest, const struct image *src,
			   const struct ct_point *src_pos)
{
  struct ct_point real_src_pos = *src_pos, real_dest_pos = { 0, 0 };
  struct ct_size real_size = { dest->width, dest->height };

  clip_two_regions(dest, src, &real_size, &real_dest_pos, &real_src_pos);
  {
    int x, y;

    for (y = 0; y < real_size.height; y++) {
      for (x = 0; x < real_size.width; x++) {
	unsigned char *psrc = IMAGE_GET_ADDRESS(src, x + real_src_pos.x,
						y + real_src_pos.y);
	unsigned char *pdest =
	    IMAGE_GET_ADDRESS(dest, x + real_dest_pos.x,
			      y + real_dest_pos.y);

	IMAGE_CHECK(src, x + real_src_pos.x, y + real_src_pos.y);
	IMAGE_CHECK(dest, x + real_dest_pos.x, y + real_dest_pos.y);
	pdest[3] = (psrc[3] * pdest[3]) / 256;
      }
    }
  }
}

/*************************************************************************
  Copy image buffer src to dest without doing any alpha-blending.
*************************************************************************/
void image_copy_full(struct image *src, struct image *dest,
		     struct ct_rect *region)
{
  int y;

  for (y = 0; y < region->height; y++) {
    unsigned char *psrc = IMAGE_GET_ADDRESS(src, region->x, 
					    y + region->y);
    unsigned char *pdest = IMAGE_GET_ADDRESS(dest, 0, y);

    IMAGE_CHECK(src, region->x, y + region->y);
    IMAGE_CHECK(src, region->x + region->width + -1, y + region->y);
    IMAGE_CHECK(dest, 0, y);
    IMAGE_CHECK(dest, region->width - 1, y);

    memcpy(pdest, psrc, 4 * region->width);
  }
}

/*************************************************************************
  Allocate and initialize an image struct.
*************************************************************************/
struct image *image_create(int width, int height)
{
  struct image *result = fc_malloc(sizeof(*result));

  result->pitch = width * 4;
  result->data = fc_malloc(result->pitch * height);
  result->width = width;
  result->height = height;
  result->full_rect.x = 0;
  result->full_rect.y = 0;
  result->full_rect.width = width;
  result->full_rect.height = height;

  return result;
}

/*************************************************************************
  Free an image struct.
*************************************************************************/
void image_destroy(struct image *image)
{
  free(image->data);
  free(image);
}

/*************************************************************************
  ...
*************************************************************************/
struct image *image_load_gfxfile(const char *filename)
{
  png_structp pngp;
  png_infop infop;
  png_int_32 width, height, x, y;
  FILE *fp;
  struct image *xi;

  fp = fc_fopen(filename, "rb");
  if (!fp) {
    freelog(LOG_FATAL, _("Failed reading PNG file: %s"), filename);
    exit(EXIT_FAILURE);
  }

  pngp = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!pngp) {
    freelog(LOG_FATAL, _("Failed creating PNG struct"));
    exit(EXIT_FAILURE);
  }

  infop = png_create_info_struct(pngp);
  if (!infop) {
    freelog(LOG_FATAL, _("Failed creating PNG struct"));
    exit(EXIT_FAILURE);
  }
  
  if (setjmp(pngp->jmpbuf)) {
    freelog(LOG_FATAL, _("Failed while reading PNG file: %s"), filename);
    exit(EXIT_FAILURE);
  }

  png_init_io(pngp, fp);
  png_read_info(pngp, infop);

  width = png_get_image_width(pngp, infop);
  height = png_get_image_height(pngp, infop);

  freelog(LOG_DEBUG, "reading '%s' (%ldx%ld) bit depth=%d color_type=%d",
	  filename, width, height, png_get_bit_depth(pngp, infop),
	  png_get_color_type(pngp, infop));

  if (png_get_color_type(pngp,infop) == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(pngp);
  }

  if (png_get_valid(pngp, infop, PNG_INFO_tRNS)) {
    png_set_tRNS_to_alpha(pngp);
  }

  if (png_get_bit_depth(pngp,infop) == 16) {
    png_set_strip_16(pngp);
  }

  if (png_get_bit_depth(pngp,infop) < 8) {
    png_set_packing(pngp);
  }

  /* Add an alpha channel for RGB images */
  if ((png_get_color_type(pngp, infop) & PNG_COLOR_MASK_ALPHA) == 0) {
    png_set_filler(pngp, 0xff, PNG_FILLER_AFTER);
  }

  xi = image_create(width, height);

  png_read_update_info(pngp, infop);

  {
    png_bytep pb;
    png_uint_32 stride = png_get_rowbytes(pngp, infop);
    png_bytep buf = fc_malloc(stride * height);
    png_bytep *row_pointers = fc_malloc(height * sizeof(png_bytep));

    assert(stride >= width * 4);

    for (y = 0, pb = buf; y < height; y++, pb += stride) {
      row_pointers[y] = pb;
    }

    png_read_image(pngp, row_pointers);
    png_read_end(pngp, infop);
    free(row_pointers);
    png_destroy_read_struct(&pngp, &infop, NULL);
    fclose(fp);

    pb = buf;

    for (y = 0; y < height; y++) {
      for (x = 0; x < width; x++) {
	png_bytep src = pb + 4 * x;
	unsigned char *dest = IMAGE_GET_ADDRESS(xi, x, y);

        memcpy(dest, src, 4);
      }
      pb += stride;
    }
    free(buf);
  }

  return xi;
}
