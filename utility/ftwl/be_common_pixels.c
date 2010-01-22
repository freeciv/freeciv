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

#include "log.h"
#include "mem.h"
#include "support.h"

#include "back_end.h"
#include "be_common_pixels.h"

#include <ft2build.h>
#include FT_FREETYPE_H

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
  Blit one osda onto another, using transparency value given on all 
  pixels that have alpha mask set. dest_pos and src_pos can be NULL.
*************************************************************************/
void be_copy_osda_to_osda(struct osda *dest,
			  struct osda *src,
			  const struct ct_size *size,
			  const struct ct_point *dest_pos,
			  const struct ct_point *src_pos)
{
  struct ct_point tmp_pos = { 0, 0 };

  if (!src_pos) {
    src_pos = &tmp_pos;
  }

  if (!dest_pos) {
    dest_pos = &tmp_pos;
  }

  if (!ct_size_empty(size)) {
    image_copy(dest->image, src->image, size, dest_pos, src_pos);
  }
}

/*************************************************************************
  Draw the given bitmap (ie a 1bpp pixmap) on the given osda in the given
  color and at the given position.
*************************************************************************/
void be_draw_bitmap(struct osda *target, be_color color,
		    const struct ct_point *position,
		    struct FT_Bitmap_ *bitmap)
{
  if (bitmap->pixel_mode == ft_pixel_mode_mono) {
    draw_mono_bitmap(target->image, color, position, bitmap);
  } else if (bitmap->pixel_mode == ft_pixel_mode_grays) {
    assert(bitmap->num_grays == 256);
    draw_alpha_bitmap(target->image, color, position, bitmap);
  } else {
    assert(0);
  }
}

/*************************************************************************
  Allocate and initialize an osda (off-screen drawing area).
*************************************************************************/
struct osda *be_create_osda(int width, int height)
{
  struct osda *result = fc_malloc(sizeof(*result));

  result->image = image_create(width, height);
  result->magic = 11223344;

  return result;
}

/*************************************************************************
  Free an allocated osda.
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
  Put size of osda into size.
*************************************************************************/
void be_osda_get_size(struct ct_size *size, const struct osda *osda)
{
  size->width = osda->image->width;
  size->height = osda->image->height;
}

/*************************************************************************
  ...
*************************************************************************/
void be_multiply_alphas(struct sprite *dest, const struct sprite *src,
                        const struct ct_point *src_pos)
{
  image_multiply_alphas(dest->image, src->image, src_pos);
}

/*************************************************************************
  ...
*************************************************************************/
static struct sprite *ctor_sprite(struct image *image)
{
  struct sprite *result = fc_malloc(sizeof(struct sprite));
  result->image = image;
  return result;
}

/*************************************************************************
  ...
*************************************************************************/
void be_free_sprite(struct sprite *sprite)
{
  free(sprite);
}

/*************************************************************************
  ...
*************************************************************************/
struct sprite *be_crop_sprite(struct sprite *source,
			      int x, int y, int width, int height)
{
  struct sprite *result = ctor_sprite(image_create(width, height));
  struct ct_rect region = { x, y, width, height };

  ct_clip_rect(&region, &source->image->full_rect);

  image_copy_full(source->image, result->image, &region);

  return result;
}

/*************************************************************************
  ...
*************************************************************************/
struct sprite *be_load_gfxfile(const char *filename)
{
  struct image *gfx = image_load_gfxfile(filename);

  return ctor_sprite(gfx);
}

/*************************************************************************
  ...
*************************************************************************/
void be_sprite_get_size(struct ct_size *size, const struct sprite *sprite)
{
  size->width = sprite->image->width;
  size->height = sprite->image->height;
}


/* ========== drawing ================ */


/*************************************************************************
  Draw an empty rectangle in given osda with given drawing type.
*************************************************************************/
void be_draw_rectangle(struct osda *target, const struct ct_rect *spec,
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

    be_draw_line(target, &nw, &ne, 1, FALSE, color);
    be_draw_line(target, &sw, &se, 1, FALSE, color);
    be_draw_line(target, &nw, &sw, 1, FALSE, color);
    be_draw_line(target, &ne, &se, 1, FALSE, color);
  }
}

/*************************************************************************
  Draw a vertical line (only).
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
  Draw a horisontal line (only).
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
  Draw any line.
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
    struct ct_point pos = { x, y };

    if (ct_point_in_rect(&pos, &image->full_rect)) {
      IMAGE_CHECK(image, x, y);
      memcpy(IMAGE_GET_ADDRESS(image, x, y), src, 4);
    }
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
  Draw a line in given osda with given drawing type.
*************************************************************************/
void be_draw_line(struct osda *target,
		  const struct ct_point *start,
		  const struct ct_point *end,
		  int line_width, bool dashed, be_color color)
{
  unsigned char tmp[4];
  struct ct_rect bounds =
      { 0, 0, target->image->width, target->image->height };

  set_color(color, tmp);

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
    draw_line(target->image, tmp, start->x, start->y, end->x, end->y,
	      line_width, dashed);
  }
}

/*************************************************************************
  Fill a square region in given osda with given colour and drawing type.
*************************************************************************/
void be_draw_region(struct osda *target, 
		    const struct ct_rect *region, be_color color)
{
  unsigned char tmp[4];
  int x, y;
  struct ct_rect actual = *region,
      bounds = { 0, 0, target->image->width, target->image->height };
  int width;

  set_color(color, tmp);

  ct_clip_rect(&actual, &bounds);

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
  Return TRUE iff pixel in given osda is transparent or out of bounds.
*************************************************************************/
bool be_is_transparent_pixel(struct osda *osda, const struct ct_point *pos)
{
  struct ct_rect bounds = { 0, 0, osda->image->width, osda->image->height };
  if (!ct_point_in_rect(pos, &bounds)) {
    return FALSE;
  }

  IMAGE_CHECK(osda->image, pos->x, pos->y);
  return IMAGE_GET_ADDRESS(osda->image, pos->x, pos->y)[3] != MAX_OPACITY;
}

/*************************************************************************
  size, dest_pos and src_pos can be NULL
*************************************************************************/
void be_draw_sprite(struct osda *target, 
		    const struct sprite *sprite,
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

  image_copy(target->image, sprite->image, size, dest_pos, src_pos);
}

/*************************************************************************
  Write an image buffer to file.
*************************************************************************/
void be_write_osda_to_file(struct osda *osda, const char *filename)
{
  FILE *file;
  unsigned char *line_buffer = fc_malloc(3 * osda->image->width), *pout;
  int x, y;

  file = fc_fopen(filename, "w");

  fprintf(file, "P6\n");
  fprintf(file, "%d %d\n", osda->image->width, osda->image->height);
  fprintf(file, "255\n");

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
  fclose(file);
}
