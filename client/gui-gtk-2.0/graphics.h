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
  GdkPixmap *pixmap;
  GdkPixmap *fogged;
  GdkBitmap *mask;
  int	     has_mask;
  int	     width;
  int	     height;

  GdkPixbuf *pixbuf;
};

typedef struct Sprite SPRITE;

void create_overlay_unit(struct canvas *pcanvas, int i);

extern SPRITE *    intro_gfx_sprite;
extern SPRITE *    radar_gfx_sprite;
extern GdkCursor * goto_cursor;
extern GdkCursor * drop_cursor;
extern GdkCursor * nuke_cursor;
extern GdkCursor * patrol_cursor;

void gtk_draw_shadowed_string(GdkDrawable *drawable,
			      GdkGC *black_gc,
			      GdkGC *white_gc,
			      gint x, gint y, PangoLayout *layout);

SPRITE *ctor_sprite_mask(GdkPixmap *mypixmap, GdkPixmap *mask,
			 int width, int height);
SPRITE* sprite_scale(SPRITE *src, int new_w, int new_h);
void sprite_get_bounding_box(SPRITE * sprite, int *start_x,
			     int *start_y, int *end_x, int *end_y);
SPRITE *crop_blankspace(SPRITE *s);

GdkPixbuf *gdk_pixbuf_new_from_sprite(SPRITE *src);
  
/********************************************************************
 NOTE: the pixmap and mask of a sprite must not change after this
       function is called!
 ********************************************************************/
GdkPixbuf *sprite_get_pixbuf(SPRITE *sprite);

#endif  /* FC__GRAPHICS_H */

