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
#ifndef FC__GRAPHICS_H
#define FC__GRAPHICS_H

#include <gtk/gtk.h>

#include "graphics_g.h"
#include "mapview_common.h"

struct Sprite
{
  /* A pixmap + mask is used if there's a 1-bit alpha channel.  mask may be
   * NULL if there's no alpha.  For multi-bit alpha levels, a pixbuf will be
   * used instead.  For consistency a pixbuf may be generated on-demand when
   * doing drawing (into a gtkpixcomm or gtkimage), so it's important that
   * the sprite data not be changed after the sprite is loaded. */
  GdkPixmap *pixmap, *pixmap_fogged;
  GdkBitmap *mask;
  GdkPixbuf *pixbuf, *pixbuf_fogged;

  int	     width;
  int	     height;
};

typedef struct Sprite SPRITE;

void create_overlay_unit(struct canvas *pcanvas, int i);

extern SPRITE *    intro_gfx_sprite;
extern SPRITE *    radar_gfx_sprite;

/* This name is to avoid a naming conflict with a global 'cursors'
 * variable in GTK+-2.6.  See PR#12459. */
extern GdkCursor *fc_cursors[CURSOR_LAST];

void gtk_draw_shadowed_string(GdkDrawable *drawable,
			      GdkGC *black_gc,
			      GdkGC *white_gc,
			      gint x, gint y, PangoLayout *layout);

SPRITE *ctor_sprite(GdkPixbuf *pixbuf);
SPRITE* sprite_scale(SPRITE *src, int new_w, int new_h);
void sprite_get_bounding_box(SPRITE * sprite, int *start_x,
			     int *start_y, int *end_x, int *end_y);
SPRITE *crop_blankspace(SPRITE *s);

/********************************************************************
  Note: a sprite cannot be changed after these functions are called!
 ********************************************************************/
GdkPixbuf *sprite_get_pixbuf(SPRITE *sprite);
GdkBitmap *sprite_get_mask(struct Sprite *sprite);

#endif  /* FC__GRAPHICS_H */

