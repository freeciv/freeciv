/**********************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Project
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

#include "log.h"
#include "mem.h"
#include "support.h"

#include "back_end.h"
#include "be_common_24.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#define DISABLE_TRANSPARENCY 0

/*************************************************************************
  Combine RGB colour values into a 24bpp colour value.
*************************************************************************/
be_color be_get_color(int red, int green, int blue)
{
  assert(red >= 0 && red <= 255);
  assert(green >= 0 && green <= 255);
  assert(blue >= 0 && blue <= 255);

  return red << 16 | green << 8 | blue;
}

/*************************************************************************
  ...
*************************************************************************/
static void set_color(be_color color, unsigned char *dest)
{
  dest[0] = ((color >> 16) & 0xff);
  dest[1] = ((color >> 8) & 0xff);
  dest[2] = ((color) & 0xff);
}

/*************************************************************************
  ...
*************************************************************************/
static unsigned char get_mask(enum be_draw_type draw_type)
{
  if (draw_type == BE_OPAQUE) {
    return MASK_OPAQUE;
  } else if (draw_type == BE_ALPHA) {
    return MASK_ALPHA;
  } else {
    assert(0);
    return MASK_OPAQUE;
  }
}

/*************************************************************************
 Will copy all pixels which have a mask != 0 from src to dest.
 mask is src.

  Two versions of image_blit_masked will follow. The first is the
  vanilla one. The other is a testbed for optimizations.
*************************************************************************/
#if 1
static void image_blit_masked(const struct ct_size *size,
			      const struct image *src,
			      const struct ct_point *src_pos,
			      struct image *dest,
			      const struct ct_point *dest_pos)
{
  int y;
  int width = size->width;

  for (y = 0; y < size->height; y++) {
    int src_y = y + src_pos->y;
    unsigned char *psrc = IMAGE_GET_ADDRESS(src, src_pos->x, src_y);

    int dest_y = y + dest_pos->y;
    unsigned char *pdest = IMAGE_GET_ADDRESS(dest, dest_pos->x, dest_y);

    int x;

    IMAGE_CHECK(src, src_pos->x, src_y);
    IMAGE_CHECK(dest, dest_pos->x, dest_y);

    for (x = 0; x < width; x++) {
      if (psrc[3] != 0) {
	memcpy(pdest, psrc, 4);
      }
      psrc += 4;
      pdest += 4;
    }
  }
}
#else
#define DUFFS_LOOP8(pixel_copy_increment, width)			\
{ int n = (width+7)/8;							\
	switch (width & 7) {						\
	case 0: do {	pixel_copy_increment;				\
	case 7:		pixel_copy_increment;				\
	case 6:		pixel_copy_increment;				\
	case 5:		pixel_copy_increment;				\
	case 4:		pixel_copy_increment;				\
	case 3:		pixel_copy_increment;				\
	case 2:		pixel_copy_increment;				\
	case 1:		pixel_copy_increment;				\
		} while ( --n > 0 );					\
	}								\
}
/* 4-times unrolled loop */
#define DUFFS_LOOP4(pixel_copy_increment, width)			\
{ int n = (width+3)/4;							\
	switch (width & 3) {						\
	case 0: do {	pixel_copy_increment;				\
	case 3:		pixel_copy_increment;				\
	case 2:		pixel_copy_increment;				\
	case 1:		pixel_copy_increment;				\
		} while ( --n > 0 );					\
	}								\
}
#define rdtscll(val) __asm__ __volatile__ ("rdtsc" : "=A" (val))
#define PREFETCH(x) __asm__ __volatile__ ("prefetchnta %0": : "m"(*(char *)(x)))
static void image_blit_masked(const struct ct_size *size,
			      const struct image *src,
			      const struct ct_point *src_pos,
			      struct image *dest,
			      const struct ct_point *dest_pos)
{
  int y;
  int width = size->width;
  unsigned long long start, end;
  static unsigned long long total_clocks = 0, total_pixels = 0;
  static int total_blits = 0;//, total_solid = 0;

  rdtscll(start);

  for (y = 0; y < size->height; y++) {
    int src_y = y + src_pos->y;
    unsigned char *psrc = IMAGE_GET_ADDRESS(src, src_pos->x, src_y);

    int dest_y = y + dest_pos->y;
    unsigned char *pdest = IMAGE_GET_ADDRESS(dest, dest_pos->x, dest_y);

    /*
    PREFETCH(psrc);
    PREFETCH(psrc + 16);
    PREFETCH(psrc + 32);
    PREFETCH(psrc + 64);
    */

#if 1
    {
	int x;

    for (x = 0; x < width; x++) {
      switch (0) {
      case 0:
	if (psrc[3] != 0) {
	  memcpy(pdest, psrc, 4);
	}
	break;

      case 1:
	{
	  unsigned int t = *((unsigned int *) psrc);
	  unsigned int *tp = (unsigned int *) pdest;

	  if ((t & 0xff000000) != 0) {
	    *tp = t;
	  }
	}
	break;

      case 2:
	{
	  unsigned int t = *((unsigned int *) psrc);
	  unsigned int *tp = (unsigned int *) pdest;
	  unsigned int s = *tp;

	  if ((t & 0xff000000) != 0) {
	    s = t;
	  }
	  *tp = s;
	}
	break;
      }

      psrc += 4;
      pdest += 4;
    }
    }
#else
    DUFFS_LOOP4( {
		if (psrc[3] != 0) {
		    memcpy(pdest, psrc, 4);
		}
		psrc += 4; 
		pdest += 4;
    }
		 , width);
#endif
  }
  rdtscll(end);
  total_clocks += (end - start);
  total_pixels += (size->width * size->height);
  total_blits++;
  if((total_blits%1000)==0) {
      printf("%f clocks per pixel\n",(float)total_clocks/total_pixels);
  }

  /*if (trans == 0) {
    total_solid++;
  }
  */
  /* printf("%f solid blits\n",(float)total_solid/total_blits); */
  /* printf("%d transparent pixel vs %d\n",trans,non_trans); */
}
#endif

/*************************************************************************
  Will copy all pixels which have a mask == MASK_ALPHA from src to
  dest with transparency.  Will copy all pixels which have a mask 
  == MASK_OPAQUE from src to dest.
*************************************************************************/
static void image_blit_masked_trans(const struct ct_size *size,
				    const struct image *src,
				    const struct ct_point *src_pos,
				    struct image *dest,
				    const struct ct_point *dest_pos,
				    int transparency)
{
  int x, y;
  int inv_transparency = MAX_TRANSPARENCY - transparency;

  for (y = 0; y < size->height; y++) {
    for (x = 0; x < size->width; x++) {
      int src_x = x + src_pos->x;
      int src_y = y + src_pos->y;
      unsigned char *psrc = IMAGE_GET_ADDRESS(src, src_x, src_y);
      unsigned char mask_value = psrc[3];

      IMAGE_CHECK(src, src_x, src_y);

      if (mask_value != 0) {
	int dest_x = x + dest_pos->x;
	int dest_y = y + dest_pos->y;

	unsigned char *pdest = IMAGE_GET_ADDRESS(dest, dest_x, dest_y);

	IMAGE_CHECK(dest, dest_x, dest_y);

	if (mask_value == MASK_ALPHA) {
	  unsigned char red, green, blue;

	  red = ((psrc[0] * inv_transparency) +
		 (pdest[0] * transparency)) / MAX_TRANSPARENCY;
	  green = ((psrc[1] * inv_transparency) +
		   (pdest[1] * transparency)) / MAX_TRANSPARENCY;
	  blue = ((psrc[2] * inv_transparency) +
		  (pdest[2] * transparency)) / MAX_TRANSPARENCY;

	  pdest[0] = red;
	  pdest[1] = green;
	  pdest[2] = blue;
	} else if (mask_value == MASK_OPAQUE) {
	  memcpy(pdest, psrc, 4);
	} else {
	  assert(0);
	}
      }
    }
  }
}

/*************************************************************************
  ...
*************************************************************************/
static void update_masks(const struct ct_size *size,
			 const struct image *src,
			 const struct ct_point *src_pos,
			 struct image *dest, const struct ct_point *dest_pos)
{
#if 0
  int x, y;

  for (y = 0; y < size->height; y++) {
    for (x = 0; x < size->width; x++) {
      int src_x = x + src_pos->x;
      int src_y = y + src_pos->y;
      unsigned char *psrc = IMAGE_GET_ADDRESS(src, src_x, src_y);
      int dest_x = x + dest_pos->x;
      int dest_y = y + dest_pos->y;
      unsigned char *pdest = IMAGE_GET_ADDRESS(dest, dest_x, dest_y);

      pdest[3] = psrc[3];
    }
  }
#endif
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

  /*
    printf("  after clip:\n");
    printf("    size=(%dx%d)\n", real_size.width, real_size.height);
    printf("     src=(%d,%d) (%dx%d)\n", real_src_pos.x, real_src_pos.y,
	   src->width, src->height);
    printf("    dest=(%d,%d) (%dx%d)\n", real_dest_pos.x, real_dest_pos.y,
	   dest->width, dest->height);
  */

  *size=real_size;
  *src_pos=real_src_pos;
  *dest_pos=real_dest_pos;
}

/*************************************************************************
  Changes the mask in dest for all !=0 src masks.
*************************************************************************/
static void set_mask_masked(const struct ct_size *size,
			    const struct image *src,
			    const struct ct_point *src_pos,
			    struct image *dest,
			    const struct ct_point *dest_pos,
			    unsigned char mask)
{
  int x, y;
  struct ct_point real_src_pos = *src_pos, real_dest_pos = *dest_pos;
  struct ct_size real_size = *size;

  clip_two_regions(dest, src, &real_size, &real_dest_pos, &real_src_pos);

  for (y = 0; y < real_size.height; y++) {
    unsigned char *psrc = IMAGE_GET_ADDRESS(src, real_src_pos.x, 
                                            y + real_src_pos.y);
    unsigned char *pdest = IMAGE_GET_ADDRESS(dest, real_dest_pos.x, 
                                             y + real_dest_pos.y);

    IMAGE_CHECK(src, real_src_pos.x, y + real_src_pos.y);
    IMAGE_CHECK(dest, real_dest_pos.x, y + real_dest_pos.y);

    for (x = 0; x < real_size.width; x++) {
      if (psrc[3] != 0) {
	pdest[3] = mask;
      }
      psrc+=4;
      pdest+=4;
    }
  }
}

/*************************************************************************
  ...
*************************************************************************/
static void image_copy(struct image *dest,
		       struct image *src,
		       const struct ct_size *size,
		       const struct ct_point *dest_pos,
		       const struct ct_point *src_pos, int transparency)
{
  struct ct_point real_src_pos = *src_pos, real_dest_pos = *dest_pos;
  struct ct_size real_size = *size;

  clip_two_regions(dest, src, &real_size, &real_dest_pos, &real_src_pos);

  if (transparency == 0 || DISABLE_TRANSPARENCY) {
    image_blit_masked(&real_size, src, &real_src_pos, dest, &real_dest_pos);
  } else {
    image_blit_masked_trans(&real_size, src, &real_src_pos, dest,
			    &real_dest_pos, transparency);
  }
}

/*************************************************************************
  dest_pos and src_pos can be NULL
*************************************************************************/
void be_copy_osda_to_osda(struct osda *dest,
			  struct osda *src,
			  const struct ct_size *size,
			  const struct ct_point *dest_pos,
			  const struct ct_point *src_pos, int transparency)
{
  struct ct_point tmp_pos = { 0, 0 };

  if (!src_pos) {
    src_pos = &tmp_pos;
  }

  if (!dest_pos) {
    dest_pos = &tmp_pos;
  }

  if (transparency != MAX_TRANSPARENCY && !ct_size_empty(size)) {
    image_copy(dest->image, src->image, size, dest_pos, src_pos,
	       transparency);
    /* update masks */
    dest->has_transparent_pixels = dest->has_transparent_pixels
	&& src->has_transparent_pixels;
    update_masks(size, src->image, src_pos, dest->image, dest_pos);
  }
}

/*************************************************************************
  ...
*************************************************************************/
static void draw_mono_bitmap(struct image *image,
			     enum be_draw_type draw_type,
			     be_color color,
			     const struct ct_point *position,
			     struct FT_Bitmap_ *bitmap)
{
  int x, y;
  unsigned char *pbitmap = (unsigned char *) bitmap->buffer;
  unsigned char tmp[4];

  set_color(color, tmp);
  tmp[3] = get_mask(draw_type);

  for (y = 0; y < bitmap->rows; y++) {
    for (x = 0; x < bitmap->width; x++) {
      unsigned char bv = bitmap->buffer[x / 8 + bitmap->pitch * y];
      if (TEST_BIT(bv, 7 - (x % 8))) {
	unsigned char *p = 
            IMAGE_GET_ADDRESS(image, x + position->x, y + position->y);

	IMAGE_CHECK(image, x + position->x, y + position->y);
	memcpy(p, tmp, 4);
      }
    }
    pbitmap += bitmap->pitch;
  }
}

/*************************************************************************
  ...
*************************************************************************/
static void draw_alpha_bitmap(struct image *image,
			      enum be_draw_type draw_type,
			      be_color color_,
			      const struct ct_point *position,
			      struct FT_Bitmap_ *bitmap)
{
  int x, y;
  unsigned char color[4];
  unsigned char *pbitmap = (unsigned char *) bitmap->buffer;

  set_color(color_, color);
  color[3] = get_mask(draw_type);

  for (y = 0; y < bitmap->rows; y++) {
    for (x = 0; x < bitmap->width; x++) {
      unsigned short transparency = pbitmap[x];
      unsigned short inv_transparency = 256 - transparency;
      unsigned char tmp[4];
      unsigned char *p = 
               IMAGE_GET_ADDRESS(image, position->x + x, position->y + y);

      IMAGE_CHECK(image, x, y);

      tmp[0] = ((p[0] * inv_transparency) + (color[0] * transparency)) / 256;
      tmp[1] = ((p[1] * inv_transparency) + (color[1] * transparency)) / 256;
      tmp[2] = ((p[2] * inv_transparency) + (color[2] * transparency)) / 256;
      tmp[3] = color[3];

      memcpy(p, tmp, 4);

    }
    pbitmap += bitmap->pitch;
  }
}

/*************************************************************************
  ...
*************************************************************************/
void be_draw_bitmap(struct osda *target, enum be_draw_type draw_type,
		    be_color color,
		    const struct ct_point *position,
		    struct FT_Bitmap_ *bitmap)
{
  if (bitmap->pixel_mode == ft_pixel_mode_mono) {
    draw_mono_bitmap(target->image, draw_type, color, position, bitmap);
  } else if (bitmap->pixel_mode == ft_pixel_mode_grays) {
    assert(bitmap->num_grays == 256);
    draw_alpha_bitmap(target->image, draw_type, color, position, bitmap);
  } else {
    assert(0);
  }
}

/*************************************************************************
  ...
*************************************************************************/
struct osda *be_create_osda(int width, int height)
{
  struct ct_rect rect = { 0, 0, width, height };
  struct osda *result = fc_malloc(sizeof(*result));

  freelog(LOG_DEBUG, "create_osda(%dx%d)", width, height);
  result->image = image_create(width, height);
  result->has_transparent_pixels = TRUE;
  be_set_transparent(result, &rect);
  result->magic=11223344;
  return result;
}

/*************************************************************************
  ...
*************************************************************************/
void be_free_osda(struct osda *osda)
{
    /*
  assert(osda->magic == 11223344);
  osda->magic = 0;
  image_destroy(osda->image);
  free(osda);
    */
}

/*************************************************************************
  ...
*************************************************************************/
void image_set_mask(const struct image *image, const struct ct_rect *rect,
		    unsigned char mask)
{
  int x, y;

  for (y = rect->y; y < rect->y + rect->height; y++) {
    for (x = rect->x; x < rect->x + rect->width; x++) {
      IMAGE_CHECK(image, x, y);
      IMAGE_GET_ADDRESS(image, x, y)[3] = mask;      
    }
  }
}

/*************************************************************************
  ...
*************************************************************************/
void be_set_transparent(struct osda *osda, const struct ct_rect *rect)
{
  image_set_mask(osda->image, rect, 0);
  osda->has_transparent_pixels = TRUE;
}


/* ========== drawing ================ */


/*************************************************************************
  ...
*************************************************************************/
void be_draw_rectangle(struct osda *target, enum be_draw_type draw_type,
		       const struct ct_rect *spec,
		       int line_width, be_color color)
{
  int i;

  for (i = 0; i < line_width; i++) {
    struct ct_point nw = { spec->x + i, spec->y + i }, ne = nw, sw = ne, se =
	nw;

    ne.x += spec->width  - 2 * i;
    se.x += spec->width  - 2 * i;

    sw.y += spec->height  - 2 * i;
    se.y += spec->height  - 2 * i;

    be_draw_line(target, draw_type, &nw, &ne, 1, FALSE, color);
    be_draw_line(target, draw_type, &sw, &se, 1, FALSE, color);
    be_draw_line(target, draw_type, &nw, &sw, 1, FALSE, color);
    be_draw_line(target, draw_type, &ne, &se, 1, FALSE, color);
  }
}

/*************************************************************************
  ...
*************************************************************************/
static void draw_vline(struct image *image, unsigned char *src,
		       int x, int y0, int y1, int line_width, bool dashed)
{
  int y;

  if (dashed) {
    for (y = y0; y < y1; y++) {
      if (y & (1 << 3)) {
	IMAGE_CHECK(image, x, y);
	memcpy(IMAGE_GET_ADDRESS(image, x, y), src, 4);
      }
    }
  } else {
    for (y = y0; y < y1; y++) {
      IMAGE_CHECK(image, x, y);
      memcpy(IMAGE_GET_ADDRESS(image, x, y), src, 4);
    }
  }
}

/*************************************************************************
  ...
*************************************************************************/
static void draw_hline(struct image *image, unsigned char *src,
		       int y, int x0, int x1, int line_width, bool dashed)
{
  int x;

  if (dashed) {
    for (x = x0; x < x1; x++) {
      if (x & (1 << 3)) {
	IMAGE_CHECK(image, x, y);
	memcpy(IMAGE_GET_ADDRESS(image, x, y), src, 4);
      }
    }
  } else {
    for (x = x0; x < x1; x++) {
      IMAGE_CHECK(image, x, y);
      memcpy(IMAGE_GET_ADDRESS(image, x, y), src, 4);
    }
  }
}

/*************************************************************************
  ...
*************************************************************************/
static void draw_line(struct image *image, unsigned char *src,
		      int x1, int y1, int x2, int y2, int line_width,
		      bool dashed)
{
  int dx = abs(x2 - x1);
  int dy = abs(y2 - y1);
  int xinc1, xinc2, yinc1, yinc2;
  int den, num, numadd, numpixels;
  int x, y;
  int curpixel;

  xinc1 = xinc2 = (x2 >= x1) ? 1 : -1;
  yinc1 = yinc2 = (y2 >= y1) ? 1 : -1;

  if (dx >= dy) {
    xinc1 = 0;
    yinc2 = 0;
    den = dx;
    num = dx / 2;
    numadd = dy;
    numpixels = dx;
  } else {
    xinc2 = 0;
    yinc1 = 0;
    den = dy;
    num = dy / 2;
    numadd = dx;
    numpixels = dy;
  }

  x = x1;
  y = y1;

  for (curpixel = 0; curpixel <= numpixels; curpixel++) {
    IMAGE_CHECK(image, x, y);
    memcpy(IMAGE_GET_ADDRESS(image, x, y), src, 4);
    num += numadd;
    if (num >= den) {
      num -= den;
      x += xinc1;
      y += yinc1;
    }
    x += xinc2;
    y += yinc2;
  }
}

/*************************************************************************
  ...
*************************************************************************/
void be_draw_line(struct osda *target, enum be_draw_type draw_type,
		  const struct ct_point *start,
		  const struct ct_point *end,
		  int line_width, bool dashed, be_color color)
{
  unsigned char tmp[4];
  struct ct_rect bounds =
      { 0, 0, target->image->width, target->image->height };

  set_color(color, tmp);
  tmp[3] = get_mask(draw_type);

  if (start->x == end->x) {
    struct ct_point start2 = *start;
    struct ct_point end2 = *end;

    ct_clip_point(&start2, &bounds);
    ct_clip_point(&end2, &bounds);

    draw_vline(target->image, tmp, start2.x, MIN(start2.y, end2.y),
	       MAX(start2.y, end2.y), line_width, dashed);
  } else if (start->y == end->y) {
    struct ct_point start2 = *start;
    struct ct_point end2 = *end;

    ct_clip_point(&start2, &bounds);
    ct_clip_point(&end2, &bounds);

    draw_hline(target->image, tmp, start2.y, MIN(start2.x, end2.x),
	       MAX(start2.x, end2.x), line_width, dashed);
  } else {
    assert(ct_point_in_rect(start, &bounds));
    assert(ct_point_in_rect(end, &bounds));

    draw_line(target->image, tmp, start->x, start->y, end->x, end->y,
	      line_width, dashed);
  }
}

/*************************************************************************
  ...
*************************************************************************/
void be_draw_region(struct osda *target, enum be_draw_type draw_type,
		    const struct ct_rect *region, be_color color)
{
  unsigned char tmp[4];
  int x, y;
  struct ct_rect actual = *region,
      bounds = { 0, 0, target->image->width, target->image->height };
  int width;

  set_color(color, tmp);
  tmp[3] = get_mask(draw_type);

  /*
    freelog(LOG_NORMAL,"draw_region(): actual=%s",ct_rect_to_string(&actual));
    freelog(LOG_NORMAL,"  bounds=%s",ct_rect_to_string(&bounds));
  */
  ct_clip_rect(&actual, &bounds);
  /* freelog(LOG_NORMAL,"  actual=%s",ct_rect_to_string(&actual)); */

  width = actual.width;
  for (y = actual.y; y < actual.y + actual.height; y++) {
    unsigned char *pdest = IMAGE_GET_ADDRESS(target->image, actual.x, y);
    IMAGE_CHECK(target->image, actual.x, y);

    for (x = 0; x < width; x++) {
      memcpy(pdest, tmp, 4);
      pdest += 4;
    }
  }
}

/*************************************************************************
  ...
*************************************************************************/
bool be_is_transparent_pixel(struct osda *osda, const struct ct_point *pos)
{
  struct ct_rect bounds = { 0, 0, osda->image->width, osda->image->height };
  if (!ct_point_in_rect(pos, &bounds)) {
    return FALSE;
  }

  IMAGE_CHECK(osda->image, pos->x, pos->y);
  return IMAGE_GET_ADDRESS(osda->image, pos->x, pos->y)[3] == 0;
}

/*************************************************************************
  size, dest_pos and src_pos can be NULL
*************************************************************************/
void be_draw_sprite(struct osda *target, enum be_draw_type draw_type,
		    const struct Sprite *sprite,
		    const struct ct_size *size,
		    const struct ct_point *dest_pos,
		    const struct ct_point *src_pos)
{
  struct ct_size tmp_size;
  struct ct_point tmp_pos = { 0, 0 };

  if (!src_pos) {
    src_pos = &tmp_pos;
  }

  if (!dest_pos) {
    dest_pos = &tmp_pos;
  }

  if (!size) {
    tmp_size.width = sprite->image->width;
    tmp_size.height = sprite->image->height;
    size = &tmp_size;
  }

  image_copy(target->image, sprite->image, size, dest_pos, src_pos, 0);

  set_mask_masked(size, sprite->image, src_pos, target->image, dest_pos,
		  get_mask(draw_type));
}

/*************************************************************************
  ...
*************************************************************************/
void be_write_osda_to_file(struct osda *osda, const char *filename)
{
  FILE *file;

  file = fopen(filename, "w");

  fprintf(file, "P6\n");
  fprintf(file, "%d %d\n", osda->image->width, osda->image->height);
  fprintf(file, "255\n");

  {
    unsigned char *line_buffer = malloc(3 * osda->image->width), *pout;
    int x, y;

    for (y = 0; y < osda->image->height; y++) {
      pout = line_buffer;

      for (x = 0; x < osda->image->width; x++) {
	IMAGE_CHECK(osda->image, x, y);
	memcpy(pout, IMAGE_GET_ADDRESS(osda->image, x, y), 3);
	pout += 3;
      }
      fwrite(line_buffer, 3 * osda->image->width, 1, file);
    }
    free(line_buffer);
  }
  fclose(file);
}

/*************************************************************************
  ...
*************************************************************************/
void image_copy_full(struct image *src, struct image *dest,
		     struct ct_rect *region)
{
  int x, y;

  for (y = 0; y < region->height; y++) {
    for (x = 0; x < region->width; x++) {
      unsigned char *psrc = IMAGE_GET_ADDRESS(src, x + region->x, 
                                              y + region->y);
      unsigned char *pdest = IMAGE_GET_ADDRESS(dest, x, y);

      IMAGE_CHECK(src, x + region->x, y + region->y);
      IMAGE_CHECK(dest, x, y);
      memcpy(pdest, psrc, 4);
    }
  }
}

/*************************************************************************
  ...
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
  ...
*************************************************************************/
void image_destroy(struct image *image)
{
  free(image->data);
  free(image);
}

/*************************************************************************
  ...
*************************************************************************/
void be_osda_get_size(struct ct_size *size, const struct osda *osda)
{
  size->width = osda->image->width;
  size->height = osda->image->height;
}
