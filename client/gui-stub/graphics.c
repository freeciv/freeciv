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

#include <stdlib.h>

#include "tilespec.h"

#include "graphics.h"

struct Sprite *intro_gfx_sprite;
struct Sprite *radar_gfx_sprite;

/****************************************************************************
  Return whether the client supports isometric view (isometric tilesets).
****************************************************************************/
bool isometric_view_supported(void)
{
  /* PORTME */
  return FALSE;
}

/****************************************************************************
  Return whether the client supports "overhead" (non-isometric) view.
****************************************************************************/
bool overhead_view_supported(void)
{
  /* PORTME */
  return FALSE;
}

/****************************************************************************
  Load the introductory graphics.
****************************************************************************/
void load_intro_gfx(void)
{
  /* PORTME */
  intro_gfx_sprite = load_gfxfile(main_intro_filename);
  radar_gfx_sprite = load_gfxfile(minimap_intro_filename);
}

/****************************************************************************
  Load the cursors (mouse substitute sprites), including a goto cursor,
  an airdrop cursor, a nuke cursor, and a patrol cursor.
****************************************************************************/
void load_cursors(void)
{
  /* PORTME */
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

/****************************************************************************
  Return a NULL-terminated, permanently allocated array of possible
  graphics types extensions.  Extensions listed first will be checked
  first.
****************************************************************************/
const char **gfx_fileextensions(void)
{
  /* PORTME */

  /* hack to allow stub to run */
  static const char *ext[] = {
    "png",	/* png should be the default. */
    /* ...etc... */
    NULL
  };

  return ext;
}

/****************************************************************************
  Load the given graphics file into a sprite.  This function loads an
  entire image file, which may later be broken up into individual sprites
  with crop_sprite.
****************************************************************************/
struct Sprite *load_gfxfile(const char *filename)
{
  /* PORTME */
  return NULL;
}

/****************************************************************************
  Create a new sprite by cropping and taking only the given portion of
  the image.
****************************************************************************/
struct Sprite *crop_sprite(struct Sprite *source,
			   int x, int y, int width, int height,
			   struct Sprite *mask,
			   int mask_offset_x, int mask_offset_y)
{
  /* PORTME */
  return NULL;
}

/****************************************************************************
  Find the dimensions of the sprite.
****************************************************************************/
void get_sprite_dimensions(struct Sprite *sprite, int *width, int *height)
{
  /* PORTME */
#if 0
  *width = sprite->width;
  *height = sprite->height;
#endif
}

/****************************************************************************
  Free a sprite and all associated image data.
****************************************************************************/
void free_sprite(struct Sprite *s)
{
  /* PORTME */
}
