/**********************************************************************
 Freeciv - Copyright (C) 1996-2005 - Freeciv Development Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__SPRITE_H
#define FC__SPRITE_H

#include <gtk/gtk.h>

/* client */
#include "sprite_g.h"

struct sprite
{
  cairo_surface_t *surface;
};

struct sprite *sprite_scale(struct sprite *src, int new_w, int new_h);
void sprite_get_bounding_box(struct sprite *sprite, int *start_x,
                             int *start_y, int *end_x, int *end_y);
struct sprite *crop_blankspace(struct sprite *s);

/********************************************************************
  Note: a sprite cannot be changed after these functions are called!
********************************************************************/
GdkPixbuf *sprite_get_pixbuf(struct sprite *sprite);
GdkPixbuf *surface_get_pixbuf(cairo_surface_t *surf, int width, int height);

GdkPixbuf *create_extra_pixbuf(const struct extra_type *pextra);

#endif  /* FC__SPRITE_H */
