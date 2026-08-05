/***********************************************************************
 Freeciv - Copyright (C) 1996 - The Freeciv Project

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdio.h>
#include <string.h>

/* utility */
#include "mem.h"

#include "sprite.h"

#define AGENT_SPRITE_MAX_DIMENSION 16384
#define AGENT_SPRITE_FALLBACK_DIMENSION 4096

static int agent_sprite_dimension(int value)
{
  if (value < 1) {
    return 1;
  }
  if (value > AGENT_SPRITE_MAX_DIMENSION) {
    return AGENT_SPRITE_MAX_DIMENSION;
  }
  return value;
}

static int agent_scaled_dimension(int value, float scale)
{
  double scaled = (double) value * (double) scale;

  /* This also maps NaN, zero, and negative scales to a valid minimum. */
  if (!(scaled > 1.0)) {
    return 1;
  }
  if (scaled >= AGENT_SPRITE_MAX_DIMENSION) {
    return AGENT_SPRITE_MAX_DIMENSION;
  }
  return (int) (scaled + 0.5);
}

static struct sprite *agent_sprite_new(int width, int height)
{
  struct sprite *sprite = fc_malloc(sizeof(*sprite));

  sprite->width = agent_sprite_dimension(width);
  sprite->height = agent_sprite_dimension(height);
  return sprite;
}

static bool agent_png_dimensions(const char *filename, int *width, int *height)
{
  static const unsigned char signature[8] = {
    0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a
  };
  unsigned char header[24];
  FILE *file = fopen(filename, "rb");
  size_t count;
  unsigned long parsed_width;
  unsigned long parsed_height;

  if (file == NULL) {
    return FALSE;
  }
  count = fread(header, 1, sizeof(header), file);
  fclose(file);

  if (count != sizeof(header)
      || memcmp(header, signature, sizeof(signature)) != 0
      || memcmp(header + 12, "IHDR", 4) != 0) {
    return FALSE;
  }

  parsed_width = ((unsigned long) header[16] << 24)
                 | ((unsigned long) header[17] << 16)
                 | ((unsigned long) header[18] << 8)
                 | (unsigned long) header[19];
  parsed_height = ((unsigned long) header[20] << 24)
                  | ((unsigned long) header[21] << 16)
                  | ((unsigned long) header[22] << 8)
                  | (unsigned long) header[23];
  if (parsed_width == 0 || parsed_height == 0
      || parsed_width > AGENT_SPRITE_MAX_DIMENSION
      || parsed_height > AGENT_SPRITE_MAX_DIMENSION) {
    return FALSE;
  }

  *width = (int) parsed_width;
  *height = (int) parsed_height;
  return TRUE;
}

const char **gfx_fileextensions(void)
{
  static const char *extensions[] = { "png", NULL };

  return extensions;
}

/**********************************************************************//**
  Load only dimensions. The headless client never decodes or draws pixels.
**************************************************************************/
struct sprite *gui_load_gfxfile(const char *filename, bool svgflag)
{
  int width = AGENT_SPRITE_FALLBACK_DIMENSION;
  int height = AGENT_SPRITE_FALLBACK_DIMENSION;

  (void) svgflag;
  (void) agent_png_dimensions(filename, &width, &height);
  return agent_sprite_new(width, height);
}

struct sprite *gui_crop_sprite(struct sprite *source,
                               int x, int y, int width, int height,
                               struct sprite *mask,
                               int mask_offset_x, int mask_offset_y,
                               float scale, bool smooth)
{
  (void) source;
  (void) x;
  (void) y;
  (void) mask;
  (void) mask_offset_x;
  (void) mask_offset_y;
  (void) smooth;

  return agent_sprite_new(agent_scaled_dimension(width, scale),
                          agent_scaled_dimension(height, scale));
}

struct sprite *gui_create_sprite(int width, int height, struct color *pcolor)
{
  (void) pcolor;
  return agent_sprite_new(width, height);
}

void gui_get_sprite_dimensions(struct sprite *sprite,
                               int *width, int *height)
{
  if (width != NULL) {
    *width = sprite != NULL ? sprite->width : 1;
  }
  if (height != NULL) {
    *height = sprite != NULL ? sprite->height : 1;
  }
}

void gui_free_sprite(struct sprite *sprite)
{
  free(sprite);
}

struct sprite *gui_load_gfxnumber(int num)
{
  (void) num;
  return agent_sprite_new(1, 1);
}
