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

#include <gdk_imlib.h>
#include <gtk/gtk.h>

#include "game.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "support.h"
#include "unit.h"
#include "version.h"

#include "climisc.h"
#include "mapview_g.h"
#include "options.h"
#include "tilespec.h"

#include "colors.h"
#include "gtkpixcomm.h"
#include "gui_main.h"

#include "drop_cursor.xbm"
#include "drop_cursor_mask.xbm"
#include "goto_cursor.xbm"
#include "goto_cursor_mask.xbm"
#include "nuke_cursor.xbm"
#include "nuke_cursor_mask.xbm"
#include "patrol_cursor.xbm"
#include "patrol_cursor_mask.xbm"

#include "graphics.h"

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
			      GdkFont *fontset,
			      GdkGC *black_gc,
			      GdkGC *white_gc,
			      gint x, gint y, const gchar *string)
{
  gdk_draw_string(drawable, fontset, black_gc, x + 1, y + 1, string);
  gdk_draw_string(drawable, fontset, white_gc, x, y, string);
}

/**************************************************************************
...
**************************************************************************/
void load_intro_gfx( void )
{
  int tot, y, w, descent;
  char s[64];
  GdkColor face;
  GdkGC *face_gc;
  const char *motto = freeciv_motto();

  /* metrics */

  /* get colors */

  face.red  = COLOR_MOTTO_FACE_R<<8;
  face.green= COLOR_MOTTO_FACE_G<<8;
  face.blue = COLOR_MOTTO_FACE_B<<8;

  gdk_imlib_best_color_get(&face);

  /* Main graphic */

  intro_gfx_sprite = load_gfxfile(main_intro_filename);
  tot=intro_gfx_sprite->width;

  face_gc = gdk_gc_new(root_window);

  y = intro_gfx_sprite->height - (2 * gdk_string_height(main_fontset, motto));

  w = gdk_string_width(main_fontset, motto);
  gdk_gc_set_foreground(face_gc, &face);
  gdk_draw_string(intro_gfx_sprite->pixmap, main_fontset,
		  face_gc, tot / 2 - w / 2, y, motto);

  gdk_gc_destroy(face_gc);

  /* Minimap graphic */

  radar_gfx_sprite = load_gfxfile(minimap_intro_filename);
  tot = radar_gfx_sprite->width;

  my_snprintf(s, sizeof(s), "%d.%d.%d%s",
	      MAJOR_VERSION, MINOR_VERSION,
	      PATCH_VERSION, VERSION_LABEL);

  gdk_string_extents(main_fontset, s, NULL, NULL, &w, NULL, &descent);
  y = radar_gfx_sprite->height - descent - 5;
  
  gtk_draw_shadowed_string(radar_gfx_sprite->pixmap,
			   main_fontset,
			   toplevel->style->black_gc,
			   toplevel->style->white_gc,
			   tot / 2 - w / 2, y, s);

  w  = gdk_string_width(main_fontset, word_version());
  y -= gdk_string_height(main_fontset, s) + 3;

  gtk_draw_shadowed_string(radar_gfx_sprite->pixmap,
			   main_fontset,
			   toplevel->style->black_gc,
			   toplevel->style->white_gc,
			   tot / 2 - w / 2, y, word_version());
  /* done */

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

  gdk_draw_pixmap(mypixmap, civ_gc, source->pixmap, x, y, 0, 0,
		  width, height);

  if (source->has_mask) {
    mymask = gdk_pixmap_new(mask_bitmap, width, height, 1);
    gdk_draw_rectangle(mymask, mask_bg_gc, TRUE, 0, 0, -1, -1 );

    gdk_draw_pixmap(mymask, mask_fg_gc, source->mask,
		    x, y, 0, 0, width, height);
  }

  if (mask) {
    if (mymask) {
      gdk_gc_set_function(mask_fg_gc, GDK_AND);
      gdk_draw_pixmap(mymask, mask_fg_gc, mask->mask,
		      x - mask_offset_x, y - mask_offset_y,
		      0, 0, width, height);
      gdk_gc_set_function(mask_fg_gc, GDK_OR);
    } else {
      mymask = gdk_pixmap_new(NULL, width, height, 1);
      gdk_draw_rectangle(mymask, mask_bg_gc, TRUE, 0, 0, -1, -1);

      gdk_draw_pixmap(mymask, mask_fg_gc, source->mask,
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
  gdk_bitmap_unref(pixmap);
  gdk_bitmap_unref(mask);

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
  gdk_bitmap_unref(pixmap);
  gdk_bitmap_unref(mask);

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
  gdk_bitmap_unref(pixmap);
  gdk_bitmap_unref(mask);

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
  gdk_bitmap_unref(pixmap);
  gdk_bitmap_unref(mask);
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

    mysprite->has_mask = (mask != NULL);
    mysprite->mask = mask;

    mysprite->width	= width;
    mysprite->height	= height;

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
  GdkBitmap	*m;
  GdkImlibImage *im;
  SPRITE	*mysprite;
  int		 w, h;

  if(!(im=gdk_imlib_load_image((char*)filename))) {
    freelog(LOG_FATAL, "Failed reading graphics file: %s", filename);
    exit(EXIT_FAILURE);
  }

  mysprite=fc_malloc(sizeof(struct Sprite));

  w=im->rgb_width; h=im->rgb_height;

  if(!gdk_imlib_render (im, w, h)) {
    freelog(LOG_FATAL, "failed render of sprite struct for %s", filename);
    exit(EXIT_FAILURE);
  }
  
  mysprite->pixmap    = gdk_imlib_move_image (im);
  m		      = gdk_imlib_move_mask  (im);
  mysprite->mask      = m;
  mysprite->has_mask  = (m != NULL);
  mysprite->width     = w;
  mysprite->height    = h;

  gdk_imlib_kill_image (im);

  return mysprite;
}

/***************************************************************************
   Deletes a sprite.  These things can use a lot of memory.
***************************************************************************/
void free_sprite(SPRITE * s)
{
  gdk_imlib_free_pixmap(s->pixmap);
  s->pixmap = NULL;
  free(s);
}

/***************************************************************************
 ...
***************************************************************************/
void create_overlay_unit(GtkWidget *pixcomm, int i)
{
  enum color_std bg_color;
  
  /* Give tile a background color, based on the type of unit */
  switch (get_unit_type(i)->move_type) {
    case LAND_MOVING: bg_color = COLOR_STD_GROUND; break;
    case SEA_MOVING:  bg_color = COLOR_STD_OCEAN;  break;
    case HELI_MOVING: bg_color = COLOR_STD_YELLOW; break;
    case AIR_MOVING:  bg_color = COLOR_STD_CYAN;   break;
    default:	      bg_color = COLOR_STD_BLACK;  break;
  }
  gtk_pixcomm_fill(GTK_PIXCOMM(pixcomm), colors_standard[bg_color], FALSE);

  /* If we're using flags, put one on the tile */
  if(!solid_color_behind_units)  {
    struct Sprite *flag=get_nation_by_plr(game.player_ptr)->flag_sprite;

    gtk_pixcomm_copyto(GTK_PIXCOMM(pixcomm), flag, 0, 0, FALSE);
  }

  /* Finally, put a picture of the unit in the tile */
  if(i<game.num_unit_types) {
    struct Sprite *s=get_unit_type(i)->sprite;

    gtk_pixcomm_copyto(GTK_PIXCOMM(pixcomm), s, 0, 0, FALSE);
  }
  gtk_pixcomm_changed(GTK_PIXCOMM(pixcomm));
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
 Draws a black border around the sprite. This is done by parsing the
 mask and replacing the first and last pixel in every column and row
 by a black one.
***************************************************************************/
void sprite_draw_black_border(SPRITE * sprite)
{
  GdkImage *mask_image, *pixmap_image;
  int x, y;

  pixmap_image =
      gdk_image_get(sprite->pixmap, 0, 0, sprite->width, sprite->height);

  mask_image =
      gdk_image_get(sprite->mask, 0, 0, sprite->width, sprite->height);

  /* parsing columns */
  for (x = 0; x < sprite->width; x++) {
    for (y = 0; y < sprite->height; y++) {
      if (gdk_image_get_pixel(mask_image, x, y) != 0) {
	gdk_image_put_pixel(pixmap_image, x, y, 0);
	break;
      }
    }

    for (y = sprite->height - 1; y > 0; y--) {
      if (gdk_image_get_pixel(mask_image, x, y) != 0) {
	gdk_image_put_pixel(pixmap_image, x, y, 0);
	break;
      }
    }
  }

  /* parsing rows */
  for (y = 0; y < sprite->height; y++) {
    for (x = 0; x < sprite->width; x++) {
      if (gdk_image_get_pixel(mask_image, x, y) != 0) {
	gdk_image_put_pixel(pixmap_image, x, y, 0);
	break;
      }
    }

    for (x = sprite->width - 1; x > 0; x--) {
      if (gdk_image_get_pixel(mask_image, x, y) != 0) {
	gdk_image_put_pixel(pixmap_image, x, y, 0);
	break;
      }
    }
  }

  gdk_draw_image(sprite->pixmap, civ_gc, pixmap_image, 0, 0, 0, 0,
		 sprite->width, sprite->height);

  gdk_image_destroy(mask_image);
  gdk_image_destroy(pixmap_image);
}

/***************************************************************************
  Scales a sprite. If the sprite contains a mask, the mask is scaled
  as as well. The algorithm produces rather fast but beautiful
  results.
***************************************************************************/
SPRITE* sprite_scale(SPRITE *src, int new_w, int new_h)
{
  SPRITE *dst = ctor_sprite_mask(NULL, NULL, new_w, new_h);
  GdkImage *xi_src, *xi_dst, *xb_src;
  guint32 pixel;
  int xoffset_table[4096];
  int x, xoffset, xadd, xremsum, xremadd;
  int y, yoffset, yadd, yremsum, yremadd;

  xi_src = gdk_image_get(src->pixmap, 0, 0, src->width, src->height);

  xi_dst = gdk_image_new(GDK_IMAGE_FASTEST,
			 gdk_window_get_visual(root_window), new_w, new_h);

  /* for each pixel in dst, calculate pixel offset in src */
  xadd = src->width / new_w;
  xremadd = src->width % new_w;
  xoffset = 0;
  xremsum = new_w / 2;

  for (x = 0; x < new_w; x++) {
    xoffset_table[x] = xoffset;
    xoffset += xadd;
    xremsum += xremadd;
    if (xremsum >= new_w) {
      xremsum -= new_w;
      xoffset++;
    }
  }

  yadd = src->height / new_h;
  yremadd = src->height % new_h;
  yoffset = 0;
  yremsum = new_h / 2;

  if (src->has_mask) {
    xb_src = gdk_image_get(src->mask, 0, 0, src->width, src->height);

    dst->mask = gdk_pixmap_new(root_window, new_w, new_h, 1);
    gdk_draw_rectangle(dst->mask, mask_bg_gc, TRUE, 0, 0, -1, -1);

    for (y = 0; y < new_h; y++) {
      for (x = 0; x < new_w; x++) {
	pixel = gdk_image_get_pixel(xi_src, xoffset_table[x], yoffset);
	gdk_image_put_pixel(xi_dst, x, y, pixel);

	if (gdk_image_get_pixel(xb_src, xoffset_table[x], yoffset) != 0) {
	  gdk_draw_point(dst->mask, mask_fg_gc, x, y);
	}
      }

      yoffset += yadd;
      yremsum += yremadd;
      if (yremsum >= new_h) {
	yremsum -= new_h;
	yoffset++;
      }
    }

    gdk_image_destroy(xb_src);
  } else {
    for (y = 0; y < new_h; y++) {
      for (x = 0; x < new_w; x++) {
	pixel = gdk_image_get_pixel(xi_src, xoffset_table[x], yoffset);
	gdk_image_put_pixel(xi_dst, x, y, pixel);
      }

      yoffset += yadd;
      yremsum += yremadd;
      if (yremsum >= new_h) {
	yremsum -= new_h;
	yoffset++;
      }
    }
  }

  dst->pixmap = gdk_pixmap_new(root_window, new_w, new_h, -1);
  gdk_draw_image(dst->pixmap, civ_gc, xi_dst, 0, 0, 0, 0, new_w, new_h);
  gdk_image_destroy(xi_src);
  gdk_image_destroy(xi_dst);

  dst->has_mask = src->has_mask;
  return dst;
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
      gdk_image_get(sprite->mask, 0, 0, sprite->width, sprite->height);


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

  gdk_image_destroy(mask_image);
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
