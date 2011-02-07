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
#include <fc_config.h>
#endif

#include <stdlib.h>

// gui-qt
#include "colors.h"
#include "fc_client.h"
#include "qtg_cxxside.h"

#include "sprite.h"

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
struct sprite *qtg_load_gfxfile(const char *filename)
{
  sprite *entire = new sprite;

  entire->pm = new QPixmap(filename);

  return entire;
}

/****************************************************************************
  Create a new sprite by cropping and taking only the given portion of
  the image.

  source gives the sprite that is to be cropped.

  x,y, width, height gives the rectangle to be cropped.  The pixel at
  position of the source sprite will be at (0,0) in the new sprite, and
  the new sprite will have dimensions (width, height).

  mask gives an additional mask to be used for clipping the new
  sprite. Only the transparency value of the mask is used in
  crop_sprite. The formula is: dest_trans = src_trans *
  mask_trans. Note that because the transparency is expressed as an
  integer it is common to divide it by 256 afterwards.

  mask_offset_x, mask_offset_y is the offset of the mask relative to the
  origin of the source image.  The pixel at (mask_offset_x,mask_offset_y)
  in the mask image will be used to clip pixel (0,0) in the source image
  which is pixel (-x,-y) in the new image.
****************************************************************************/
struct sprite *qtg_crop_sprite(struct sprite *source,
                               int x, int y, int width, int height,
                               struct sprite *mask,
                               int mask_offset_x, int mask_offset_y)
{
  /* FIXME: Add mask handling */

  sprite *cropped = new sprite;

  cropped->pm = new QPixmap;

  *cropped->pm = source->pm->copy(x, y, width, height);

  return cropped;
}

/****************************************************************************
  Find the dimensions of the sprite.
****************************************************************************/
void qtg_get_sprite_dimensions(struct sprite *sprite, int *width, int *height)
{
  *width = sprite->pm->width();
  *height = sprite->pm->height();
}

/****************************************************************************
  Free a sprite and all associated image data.
****************************************************************************/
void qtg_free_sprite(struct sprite *s)
{
  delete s->pm;
  delete s;
}

/****************************************************************************
  Create a new sprite with the given height, width and color.
****************************************************************************/
struct sprite *qtg_create_sprite(int width, int height, struct color *pcolor)
{
  struct sprite *created = new sprite;

  created->pm = new QPixmap(width, height);

  created->pm->fill(pcolor->qcolor);

  return created;
}
