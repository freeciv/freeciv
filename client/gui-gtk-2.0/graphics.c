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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include "gtkpixcomm.h"

#include "game.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "support.h"
#include "unit.h"
#include "version.h"

#include "climisc.h"
#include "colors.h"
#include "gui_main.h"
#include "mapview_g.h"
#include "options.h"
#include "tilespec.h"

#include "graphics.h"

#include "goto_cursor.xbm"
#include "goto_cursor_mask.xbm"
#include "drop_cursor.xbm"
#include "drop_cursor_mask.xbm"
#include "nuke_cursor.xbm"
#include "nuke_cursor_mask.xbm"
#include "patrol_cursor.xbm"
#include "patrol_cursor_mask.xbm"

SPRITE *		intro_gfx_sprite;
SPRITE *		radar_gfx_sprite;

GdkCursor *		goto_cursor;
GdkCursor *		drop_cursor;
GdkCursor *		nuke_cursor;
GdkCursor *		patrol_cursor;

/***************************************************************************
...
***************************************************************************/
bool isometric_view_supported(void)
{
  return TRUE;
}

/***************************************************************************
...
***************************************************************************/
bool overhead_view_supported(void)
{
  return TRUE;
}

/***************************************************************************
...
***************************************************************************/
#define COLOR_MOTTO_FACE_R    0x2D
#define COLOR_MOTTO_FACE_G    0x71
#define COLOR_MOTTO_FACE_B    0xE3

/**************************************************************************
...
**************************************************************************/
void gtk_draw_shadowed_string(GdkDrawable *drawable,
			      GdkGC *black_gc,
			      GdkGC *white_gc,
			      gint x, gint y, PangoLayout *layout)
{
  gdk_draw_layout(drawable, black_gc, x + 1, y + 1, layout);
  gdk_draw_layout(drawable, white_gc, x, y, layout);
}

/**************************************************************************
...
**************************************************************************/
void load_intro_gfx(void)
{
  int tot, y;
  char s[64];
  GdkColor face;
  GdkGC *face_gc;
  PangoContext *context;
  PangoLayout *layout;
  PangoRectangle rect;

  context = gdk_pango_context_get();
  layout = pango_layout_new(context);
  pango_layout_set_font_description(layout, main_font);

  /* get colors */
  face.red  = COLOR_MOTTO_FACE_R<<8;
  face.green= COLOR_MOTTO_FACE_G<<8;
  face.blue = COLOR_MOTTO_FACE_B<<8;
  face_gc = gdk_gc_new(root_window);

  /* Main graphic */
  intro_gfx_sprite = load_gfxfile(main_intro_filename);
  tot=intro_gfx_sprite->width;

  pango_layout_set_text(layout, freeciv_motto(), -1);
  pango_layout_get_pixel_extents(layout, &rect, NULL);

  y = intro_gfx_sprite->height-45;

  gdk_gc_set_rgb_fg_color(face_gc, &face);
  gdk_draw_layout(intro_gfx_sprite->pixmap, face_gc,
		  (tot-rect.width) / 2, y, layout);
  g_object_unref(face_gc);

  /* Minimap graphic */
  radar_gfx_sprite = load_gfxfile(minimap_intro_filename);
  tot=radar_gfx_sprite->width;

  my_snprintf(s, sizeof(s), "%d.%d.%d%s",
	      MAJOR_VERSION, MINOR_VERSION,
	      PATCH_VERSION, VERSION_LABEL);
  pango_layout_set_text(layout, s, -1);
  pango_layout_get_pixel_extents(layout, &rect, NULL);

  y = radar_gfx_sprite->height - (rect.height + 6);

  gtk_draw_shadowed_string(radar_gfx_sprite->pixmap,
			toplevel->style->black_gc,
			toplevel->style->white_gc,
			(tot - rect.width) / 2, y,
			layout);

  pango_layout_set_text(layout, word_version(), -1);
  pango_layout_get_pixel_extents(layout, &rect, NULL);
  y-=rect.height+3;

  gtk_draw_shadowed_string(radar_gfx_sprite->pixmap,
			toplevel->style->black_gc,
			toplevel->style->white_gc,
			(tot - rect.width) / 2, y,
			layout);

  /* done */
  g_object_unref(layout);
  g_object_unref(context);
  return;
}

/****************************************************************************
  Create a new sprite by cropping and taking only the given portion of
  the image.
****************************************************************************/
struct Sprite *crop_sprite(struct Sprite *source,
			   int x, int y,
			   int width, int height,
			   struct Sprite *mask,
			   int mask_offset_x, int mask_offset_y)
{
  GdkPixmap *mypixmap, *mymask = NULL;

  mypixmap = gdk_pixmap_new(root_window, width, height, -1);

  gdk_draw_drawable(mypixmap, civ_gc, source->pixmap, x, y, 0, 0,
		    width, height);

  if (source->has_mask) {
    mymask = gdk_pixmap_new(NULL, width, height, 1);
    gdk_draw_rectangle(mymask, mask_bg_gc, TRUE, 0, 0, -1, -1);

    gdk_draw_drawable(mymask, mask_fg_gc, source->mask,
		      x, y, 0, 0, width, height);
  }

  if (mask) {
    if (mymask) {
      gdk_gc_set_function(mask_fg_gc, GDK_AND);
      gdk_draw_drawable(mymask, mask_fg_gc, mask->mask,
			x - mask_offset_x, y - mask_offset_y,
			0, 0, width, height);
      gdk_gc_set_function(mask_fg_gc, GDK_OR);
    } else {
      mymask = gdk_pixmap_new(NULL, width, height, 1);
      gdk_draw_rectangle(mymask, mask_bg_gc, TRUE, 0, 0, -1, -1);

      gdk_draw_drawable(mymask, mask_fg_gc, source->mask,
			x, y, 0, 0, width, height);
    }
  }

  return ctor_sprite_mask(mypixmap, mymask, width, height);
}

/****************************************************************************
  Find the dimensions of the sprite.
****************************************************************************/
void get_sprite_dimensions(struct Sprite *sprite, int *width, int *height)
{
  *width = sprite->width;
  *height = sprite->height;
}

/***************************************************************************
...
***************************************************************************/
void load_cursors(void)
{
  GdkBitmap *pixmap, *mask;
  GdkColor *white, *black;

  white = colors_standard[COLOR_STD_WHITE];
  black = colors_standard[COLOR_STD_BLACK];

  /* goto */
  pixmap = gdk_bitmap_create_from_data(root_window, goto_cursor_bits,
				      goto_cursor_width,
				      goto_cursor_height);
  mask   = gdk_bitmap_create_from_data(root_window, goto_cursor_mask_bits,
				      goto_cursor_mask_width,
				      goto_cursor_mask_height);
  goto_cursor = gdk_cursor_new_from_pixmap(pixmap, mask,
					  white, black,
					  goto_cursor_x_hot, goto_cursor_y_hot);
  g_object_unref(pixmap);
  g_object_unref(mask);

  /* drop */
  pixmap = gdk_bitmap_create_from_data(root_window, drop_cursor_bits,
				      drop_cursor_width,
				      drop_cursor_height);
  mask   = gdk_bitmap_create_from_data(root_window, drop_cursor_mask_bits,
				      drop_cursor_mask_width,
				      drop_cursor_mask_height);
  drop_cursor = gdk_cursor_new_from_pixmap(pixmap, mask,
					  white, black,
					  drop_cursor_x_hot, drop_cursor_y_hot);
  g_object_unref(pixmap);
  g_object_unref(mask);

  /* nuke */
  pixmap = gdk_bitmap_create_from_data(root_window, nuke_cursor_bits,
				      nuke_cursor_width,
				      nuke_cursor_height);
  mask   = gdk_bitmap_create_from_data(root_window, nuke_cursor_mask_bits,
				      nuke_cursor_mask_width,
				      nuke_cursor_mask_height);
  nuke_cursor = gdk_cursor_new_from_pixmap(pixmap, mask,
					  white, black,
					  nuke_cursor_x_hot, nuke_cursor_y_hot);
  g_object_unref(pixmap);
  g_object_unref(mask);

  /* patrol */
  pixmap = gdk_bitmap_create_from_data(root_window, patrol_cursor_bits,
				      patrol_cursor_width,
				      patrol_cursor_height);
  mask   = gdk_bitmap_create_from_data(root_window, patrol_cursor_mask_bits,
				      patrol_cursor_mask_width,
				      patrol_cursor_mask_height);
  patrol_cursor = gdk_cursor_new_from_pixmap(pixmap, mask,
					     white, black,
					     patrol_cursor_x_hot, patrol_cursor_y_hot);
  g_object_unref(pixmap);
  g_object_unref(mask);
}

/***************************************************************************
 Create a new sprite with the given pixmap, dimensions, and
 (optional) mask.
***************************************************************************/
SPRITE *ctor_sprite_mask( GdkPixmap *mypixmap, GdkPixmap *mask, 
			  int width, int height )
{
    SPRITE *mysprite = fc_malloc(sizeof(SPRITE));

    mysprite->pixmap	= mypixmap;
    mysprite->fogged = NULL;
    mysprite->has_mask = (mask != NULL);
    mysprite->mask = mask;

    mysprite->width	= width;
    mysprite->height	= height;

    mysprite->pixbuf	= NULL;

    return mysprite;
}


#ifdef UNUSED
/***************************************************************************
...
***************************************************************************/
void dtor_sprite( SPRITE *mysprite )
{
    free_sprite( mysprite );
    return;
}
#endif

/***************************************************************************
 Returns the filename extensions the client supports
 Order is important.
***************************************************************************/
const char **gfx_fileextensions(void)
{
  static const char *ext[] =
  {
    "png",
    "xpm",
    NULL
  };

  return ext;
}

/***************************************************************************
...
***************************************************************************/
struct Sprite *load_gfxfile(const char *filename)
{
  GdkPixbuf *im;
  SPRITE    *mysprite;
  int	     w, h;

  if (!(im = gdk_pixbuf_new_from_file(filename, NULL))) {
    freelog(LOG_FATAL, "Failed reading graphics file: %s", filename);
    exit(EXIT_FAILURE);
  }

  mysprite=fc_malloc(sizeof(struct Sprite));

  w = gdk_pixbuf_get_width(im); h = gdk_pixbuf_get_height(im);
  gdk_pixbuf_render_pixmap_and_mask(im, &mysprite->pixmap, &mysprite->mask, 1);

  mysprite->has_mask  = (mysprite->mask != NULL);
  mysprite->width     = w;
  mysprite->height    = h;

  mysprite->pixbuf    = NULL;
  mysprite->fogged = NULL;

  g_object_unref(im);

  return mysprite;
}

/***************************************************************************
   Deletes a sprite.  These things can use a lot of memory.
***************************************************************************/
void free_sprite(SPRITE * s)
{
  if (s->pixmap) {
    g_object_unref(s->pixmap);
    s->pixmap = NULL;
  }
  if (s->mask) {
    g_object_unref(s->mask);
    s->mask = NULL;
  }
  if (s->pixbuf) {
    g_object_unref(s->pixbuf);
  }
  if (s->fogged) {
    g_object_unref(s->fogged);
  }
  free(s);
}

/***************************************************************************
 ...
***************************************************************************/
void create_overlay_unit(struct canvas *pcanvas, int i)
{
  enum color_std bg_color;
  int x1, x2, y1, y2;
  int width, height;
  struct unit_type *type = get_unit_type(i);

  sprite_get_bounding_box(type->sprite, &x1, &y1, &x2, &y2);
  if (pcanvas->type == CANVAS_PIXBUF) {
    width = gdk_pixbuf_get_width(pcanvas->v.pixbuf);
    height = gdk_pixbuf_get_height(pcanvas->v.pixbuf);
    gdk_pixbuf_fill(pcanvas->v.pixbuf, 0x00000000);
  } else {
    if (pcanvas->type == CANVAS_PIXCOMM) {
      gtk_pixcomm_clear(pcanvas->v.pixcomm);
    }

    /* Guess */
    width = UNIT_TILE_WIDTH;
    height = UNIT_TILE_HEIGHT;
  }

  if (solid_unit_icon_bg) {
    /* Give tile a background color, based on the type of unit */
    switch (type->move_type) {
      case LAND_MOVING: bg_color = COLOR_STD_GROUND; break;
      case SEA_MOVING:  bg_color = COLOR_STD_OCEAN;  break;
      case HELI_MOVING: bg_color = COLOR_STD_YELLOW; break;
      case AIR_MOVING:  bg_color = COLOR_STD_CYAN;   break;
      default:	      bg_color = COLOR_STD_BLACK;  break;
    }
    canvas_put_rectangle(pcanvas, bg_color, 0, 0, width, height);
  }

  /* Finally, put a picture of the unit in the tile */
  canvas_put_sprite(pcanvas, 0, 0, type->sprite, 
      (x2 + x1 - width) / 2, (y1 + y2 - height) / 2, 
      UNIT_TILE_WIDTH - (x2 + x1 - width) / 2, 
      UNIT_TILE_HEIGHT - (y1 + y2 - height) / 2);
}

/***************************************************************************
  This function is so that packhand.c can be gui-independent, and
  not have to deal with Sprites itself.
***************************************************************************/
void free_intro_radar_sprites(void)
{
  if (intro_gfx_sprite) {
    free_sprite(intro_gfx_sprite);
    intro_gfx_sprite=NULL;
  }
  if (radar_gfx_sprite) {
    free_sprite(radar_gfx_sprite);
    radar_gfx_sprite=NULL;
  }
}

/***************************************************************************
  Scales a sprite. If the sprite contains a mask, the mask is scaled
  as as well.
***************************************************************************/
SPRITE* sprite_scale(SPRITE *src, int new_w, int new_h)
{
  GdkPixbuf *original, *im;
  SPRITE    *mysprite;

  original = sprite_get_pixbuf(src);
  im = gdk_pixbuf_scale_simple(original, new_w, new_h, GDK_INTERP_BILINEAR);

  mysprite=fc_malloc(sizeof(struct Sprite));

  gdk_pixbuf_render_pixmap_and_mask(im, &mysprite->pixmap, &mysprite->mask, 1);

  mysprite->has_mask  = (mysprite->mask != NULL);
  mysprite->width     = new_w;
  mysprite->height    = new_h;

  mysprite->pixbuf    = NULL;
  mysprite->fogged = NULL;
  g_object_unref(im);

  return mysprite;
}

/***************************************************************************
 Method returns the bounding box of a sprite. It assumes a rectangular
 object/mask. The bounding box contains the border (pixel which have
 unset pixel as neighbours) pixel.
***************************************************************************/
void sprite_get_bounding_box(SPRITE * sprite, int *start_x,
			     int *start_y, int *end_x, int *end_y)
{
  GdkImage *mask_image;
  int i, j;

  if (!sprite->has_mask || !sprite->mask) {
    *start_x = 0;
    *start_y = 0;
    *end_x = sprite->width - 1;
    *end_y = sprite->height - 1;
    return;
  }

  mask_image =
    gdk_drawable_get_image(sprite->mask, 0, 0, sprite->width, sprite->height);


  /* parses mask image for the first column that contains a visible pixel */
  *start_x = -1;
  for (i = 0; i < sprite->width && *start_x == -1; i++) {
    for (j = 0; j < sprite->height; j++) {
      if (gdk_image_get_pixel(mask_image, i, j) != 0) {
	*start_x = i;
	break;
      }
    }
  }

  /* parses mask image for the last column that contains a visible pixel */
  *end_x = -1;
  for (i = sprite->width - 1; i >= *start_x && *end_x == -1; i--) {
    for (j = 0; j < sprite->height; j++) {
      if (gdk_image_get_pixel(mask_image, i, j) != 0) {
	*end_x = i;
	break;
      }
    }
  }

  /* parses mask image for the first row that contains a visible pixel */
  *start_y = -1;
  for (i = 0; i < sprite->height && *start_y == -1; i++) {
    for (j = *start_x; j <= *end_x; j++) {
      if (gdk_image_get_pixel(mask_image, j, i) != 0) {
	*start_y = i;
	break;
      }
    }
  }

  /* parses mask image for the last row that contains a visible pixel */
  *end_y = -1;
  for (i = sprite->height - 1; i >= *end_y && *end_y == -1; i--) {
    for (j = *start_x; j <= *end_x; j++) {
      if (gdk_image_get_pixel(mask_image, j, i) != 0) {
	*end_y = i;
	break;
      }
    }
  }

  g_object_unref(mask_image);
}

/*********************************************************************
 Crops all blankspace from a sprite (insofar as is possible as a rectangle)
*********************************************************************/
SPRITE *crop_blankspace(SPRITE *s)
{
  int x1, y1, x2, y2;

  sprite_get_bounding_box(s, &x1, &y1, &x2, &y2);

  return crop_sprite(s, x1, y1, x2 - x1 + 1, y2 - y1 + 1, NULL, -1, -1);
}

/*********************************************************************
 Converts a sprite to a GdkPixbuf.
*********************************************************************/
GdkPixbuf *gdk_pixbuf_new_from_sprite(SPRITE *src)
{
  GdkPixbuf *dst;
  int w, h;

  w = src->width;
  h = src->height;
  
  /* convert pixmap */
  dst = gdk_pixbuf_new(GDK_COLORSPACE_RGB, src->mask != NULL, 8, w, h);
  gdk_pixbuf_get_from_drawable(dst, src->pixmap, NULL, 0, 0, 0, 0, w, h);

  /* convert mask */
  if (src->mask) {
    GdkImage *img;
    int x, y, rowstride;
    guchar *pixels;

    img = gdk_drawable_get_image(src->mask, 0, 0, w, h);

    pixels = gdk_pixbuf_get_pixels(dst);
    rowstride = gdk_pixbuf_get_rowstride(dst);

    for (y = 0; y < h; y++) {
      for (x = 0; x < w; x++) {
	guchar *pixel = pixels + y * rowstride + x * 4 + 3;

	if (gdk_image_get_pixel(img, x, y)) {
	  *pixel = 255;
	} else {
	  *pixel = 0;
	}
      }
    }
    g_object_unref(img);
  }

  return dst;
}

/********************************************************************
 NOTE: the pixmap and mask of a sprite must not change after this
       function is called!
 ********************************************************************/
GdkPixbuf *sprite_get_pixbuf(SPRITE *sprite)
{
  if (!sprite) {
    return NULL;
  }
  
  if (!sprite->pixbuf) {
    sprite->pixbuf = gdk_pixbuf_new_from_sprite(sprite);
  }
  return sprite->pixbuf;
}

