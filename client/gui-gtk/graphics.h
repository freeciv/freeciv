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

struct Sprite
{
  GdkPixmap *pixmap;
  GdkBitmap *mask;
  int	     has_mask;
  int	     width;
  int	     height;
};

typedef struct Sprite SPRITE;

void	create_overlay_unit	(GtkWidget *pixcomm, int i);

extern SPRITE *    intro_gfx_sprite;
extern SPRITE *    radar_gfx_sprite;
extern GdkCursor * goto_cursor;
extern GdkCursor * drop_cursor;
extern GdkCursor * nuke_cursor;
extern GdkCursor * patrol_cursor;

void gtk_draw_shadowed_string(GdkDrawable *drawable,
			      GdkFont *fontset,
			      GdkGC *black_gc,
			      GdkGC *white_gc,
			      gint x, gint y, const gchar *string);

SPRITE *ctor_sprite_mask(GdkPixmap *mypixmap, GdkPixmap *mask,
			 int width, int height);
void sprite_draw_black_border(SPRITE * sprite);
SPRITE* sprite_scale(SPRITE *src, int new_w, int new_h);
void sprite_get_bounding_box(SPRITE * sprite, int *start_x,
			     int *start_y, int *end_x, int *end_y);
SPRITE *crop_blankspace(SPRITE *s);

#endif  /* FC__GRAPHICS_H */
