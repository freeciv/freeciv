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
#include <ctype.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>

#ifdef XPM_H_NO_X11
#include <xpm.h>
#else
#include <X11/xpm.h>
#endif

#include "fcintl.h"
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

struct Sprite *intro_gfx_sprite;
struct Sprite *radar_gfx_sprite;

Cursor goto_cursor;
Cursor drop_cursor;
Cursor nuke_cursor;
Cursor patrol_cursor;

static struct Sprite *ctor_sprite_mask(Pixmap mypixmap, Pixmap mask, 
 				       int width, int height);

/***************************************************************************
...
***************************************************************************/
bool isometric_view_supported(void)
{
  return FALSE;
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
#define COLOR_MOTTO_FACE    "#2D71E3"

void load_intro_gfx(void)
{
  int tot, lin, y, w;
  char s[64];
  XColor face;
  int have_face;
  char *motto = freeciv_motto();

  /* metrics */

  lin=main_font_struct->ascent+main_font_struct->descent;

  /* get colors */

  if(XParseColor(display, cmap, COLOR_MOTTO_FACE, &face) &&
     XAllocColor(display, cmap, &face)) {
    have_face = TRUE;
  } else {
    face.pixel = colors_standard[COLOR_STD_WHITE];
    have_face = FALSE;
  }

  /* Main graphic */

  intro_gfx_sprite=load_gfxfile(main_intro_filename);
  tot=intro_gfx_sprite->width;

  y=intro_gfx_sprite->height-(2*lin);

  w=XTextWidth(main_font_struct, motto, strlen(motto));
  XSetForeground(display, font_gc, face.pixel);
  XDrawString(display, intro_gfx_sprite->pixmap, font_gc, 
	      tot/2-w/2, y, 
	      motto, strlen(motto));

  /* Minimap graphic */

  radar_gfx_sprite=load_gfxfile(minimap_intro_filename);
  tot=radar_gfx_sprite->width;

  y=radar_gfx_sprite->height-(lin+((int)(1.5*main_font_struct->descent)));

  w=XTextWidth(main_font_struct, word_version(), strlen(word_version()));
  XSetForeground(display, font_gc, colors_standard[COLOR_STD_BLACK]);
  XDrawString(display, radar_gfx_sprite->pixmap, font_gc, 
	      (tot/2-w/2)+1, y+1, 
	      word_version(), strlen(word_version()));
  XSetForeground(display, font_gc, colors_standard[COLOR_STD_WHITE]);
  XDrawString(display, radar_gfx_sprite->pixmap, font_gc, 
	      tot/2-w/2, y, 
	      word_version(), strlen(word_version()));

  y+=lin;

  my_snprintf(s, sizeof(s), "%d.%d.%d%s",
	      MAJOR_VERSION, MINOR_VERSION,
	      PATCH_VERSION, VERSION_LABEL);
  w=XTextWidth(main_font_struct, s, strlen(s));
  XSetForeground(display, font_gc, colors_standard[COLOR_STD_BLACK]);
  XDrawString(display, radar_gfx_sprite->pixmap, font_gc, 
	      (tot/2-w/2)+1, y+1, s, strlen(s));
  XSetForeground(display, font_gc, colors_standard[COLOR_STD_WHITE]);
  XDrawString(display, radar_gfx_sprite->pixmap, font_gc, 
	      tot/2-w/2, y, s, strlen(s));

  /* free colors */

  if (have_face)
    XFreeColors(display, cmap, &(face.pixel), 1, 0);

  /* done */

  return;
}

/***************************************************************************
return newly allocated sprite cropped from source
***************************************************************************/
struct Sprite *crop_sprite(struct Sprite *source,
			   int x, int y, int width, int height)
{
  GC plane_gc;
  Pixmap mypixmap, mask;

  mypixmap = XCreatePixmap(display, root_window,
			   width, height, display_depth);
  XCopyArea(display, source->pixmap, mypixmap, civ_gc, 
	    x, y, width, height, 0, 0);

  mask = XCreatePixmap(display, root_window, width, height, 1);

  plane_gc = XCreateGC(display, mask, 0, NULL);
  XCopyArea(display, source->mask, mask, plane_gc, 
	    x, y, width, height, 0, 0);
  XFreeGC(display, plane_gc);

  return ctor_sprite_mask(mypixmap, mask, width, height);
}

/***************************************************************************
...
***************************************************************************/
void load_cursors(void)
{
  Pixmap pixmap, mask;
  XColor white, black;

  white.pixel = colors_standard[COLOR_STD_WHITE];
  black.pixel = colors_standard[COLOR_STD_BLACK];
  XQueryColor(display, cmap, &white);
  XQueryColor(display, cmap, &black);

  /* goto */
  pixmap = XCreateBitmapFromData(display, root_window, goto_cursor_bits,
				 goto_cursor_width, goto_cursor_height);
  mask   = XCreateBitmapFromData(display, root_window, goto_cursor_mask_bits,
				 goto_cursor_mask_width, goto_cursor_mask_height);
  goto_cursor = XCreatePixmapCursor(display, pixmap, mask,
				    &white, &black,
				    goto_cursor_x_hot, goto_cursor_y_hot);
  XFreePixmap(display, pixmap);
  XFreePixmap(display, mask);

  /* drop */
  pixmap = XCreateBitmapFromData(display, root_window, drop_cursor_bits,
				 drop_cursor_width, drop_cursor_height);
  mask   = XCreateBitmapFromData(display, root_window, drop_cursor_mask_bits,
				 drop_cursor_mask_width, drop_cursor_mask_height);
  drop_cursor = XCreatePixmapCursor(display, pixmap, mask,
				    &white, &black,
				    drop_cursor_x_hot, drop_cursor_y_hot);
  XFreePixmap(display, pixmap);
  XFreePixmap(display, mask);

  /* nuke */
  pixmap = XCreateBitmapFromData(display, root_window, nuke_cursor_bits,
				 nuke_cursor_width, nuke_cursor_height);
  mask   = XCreateBitmapFromData(display, root_window, nuke_cursor_mask_bits,
				 nuke_cursor_mask_width, nuke_cursor_mask_height);
  nuke_cursor = XCreatePixmapCursor(display, pixmap, mask,
				    &white, &black,
				    nuke_cursor_x_hot, nuke_cursor_y_hot);
  XFreePixmap(display, pixmap);
  XFreePixmap(display, mask);

  /* patrol */
  pixmap = XCreateBitmapFromData(display, root_window, patrol_cursor_bits,
				 patrol_cursor_width, patrol_cursor_height);
  mask   = XCreateBitmapFromData(display, root_window, patrol_cursor_mask_bits,
				 patrol_cursor_mask_width, patrol_cursor_mask_height);
  patrol_cursor = XCreatePixmapCursor(display, pixmap, mask,
				      &white, &black,
				      patrol_cursor_x_hot, patrol_cursor_y_hot);
  XFreePixmap(display, pixmap);
  XFreePixmap(display, mask);
}

#ifdef UNUSED
/***************************************************************************
...
***************************************************************************/
static struct Sprite *ctor_sprite(Pixmap mypixmap, int width, int height)
{
  struct Sprite *mysprite=fc_malloc(sizeof(struct Sprite));
  mysprite->pixmap=mypixmap;
  mysprite->width=width;
  mysprite->height=height;
  mysprite->has_mask=0;
  return mysprite;
}
#endif

/***************************************************************************
...
***************************************************************************/
static struct Sprite *ctor_sprite_mask(Pixmap mypixmap, Pixmap mask, 
				       int width, int height)
{
  struct Sprite *mysprite=fc_malloc(sizeof(struct Sprite));
  mysprite->pixmap=mypixmap;
  mysprite->mask=mask;

  mysprite->width=width;
  mysprite->height=height;
  mysprite->has_mask=1;
  return mysprite;
}



#ifdef UNUSED
/***************************************************************************
...
***************************************************************************/
void dtor_sprite(struct Sprite *mysprite)
{
  XFreePixmap(display, mysprite->pixmap);
  if(mysprite->has_mask)
    XFreePixmap(display, mysprite->mask);
  free_colors(mysprite->pcolorarray, mysprite->ncols);
  free(mysprite->pcolorarray);
  free(mysprite);

}
#endif

/***************************************************************************
 Returns the filename extensions the client supports
 Order is important.
***************************************************************************/
char **gfx_fileextensions(void)
{
  static char *ext[] =
  {
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
  struct Sprite *mysprite;
  Pixmap mypixmap, mask_bitmap;
  int err;
  XpmAttributes attributes;
  
  attributes.extensions = NULL;
  attributes.valuemask = XpmCloseness|XpmColormap;
  attributes.colormap = cmap;
  attributes.closeness = 40000;

again:
  
  if((err=XpmReadFileToPixmap(display, root_window, (char*)filename, &mypixmap, 
			      &mask_bitmap, &attributes))!=XpmSuccess) {
    if(err==XpmColorError || err==XpmColorFailed) {
      color_error();
      goto again;
    }
    freelog(LOG_FATAL, _("Failed reading XPM file: %s"), filename);
    exit(EXIT_FAILURE);
  }

  mysprite=fc_malloc(sizeof(struct Sprite));
  
  mysprite->pixmap=mypixmap;
  mysprite->mask=mask_bitmap;
  mysprite->has_mask=(mask_bitmap!=0);
  mysprite->width=attributes.width;
  mysprite->height=attributes.height;

  return mysprite;
}

/***************************************************************************
   Deletes a sprite.  These things can use a lot of memory.
   
   (How/why does this differ from dtor_sprite() ?  --dwp)
***************************************************************************/
void free_sprite(struct Sprite *s)
{
  if(s->pixmap) XFreePixmap(display,s->pixmap);
  if(s->has_mask) XFreePixmap(display,s->mask);
  free(s);
}


/***************************************************************************
...
***************************************************************************/
Pixmap create_overlay_unit(int i)
{
  Pixmap pm;
  enum color_std bg_color;
  
  pm=XCreatePixmap(display, root_window, 
		   NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT, display_depth);

  /* Give tile a background color, based on the type of unit */
  switch (get_unit_type(i)->move_type) {
    case LAND_MOVING: bg_color = COLOR_STD_GROUND; break;
    case SEA_MOVING:  bg_color = COLOR_STD_OCEAN; break;
    case HELI_MOVING: bg_color = COLOR_STD_YELLOW; break;
    case AIR_MOVING:  bg_color = COLOR_STD_CYAN; break;
    default:          bg_color = COLOR_STD_BLACK; break;
  }
  XSetForeground(display, fill_bg_gc, colors_standard[bg_color]);
  XFillRectangle(display, pm, fill_bg_gc, 0,0, 
		 NORMAL_TILE_WIDTH,NORMAL_TILE_HEIGHT);

  /* If we're using flags, put one on the tile */
  if(!solid_color_behind_units)  {
    struct Sprite *flag=get_nation_by_plr(game.player_ptr)->flag_sprite;

    XSetClipOrigin(display, civ_gc, 0,0);
    XSetClipMask(display, civ_gc, flag->mask);
    XCopyArea(display, flag->pixmap, pm, civ_gc, 0,0, 
    	      flag->width,flag->height, 0,0);
    XSetClipMask(display, civ_gc, None);
  }

  /* Finally, put a picture of the unit in the tile */
  if(i<game.num_unit_types) {
    struct Sprite *s=get_unit_type(i)->sprite;

    XSetClipOrigin(display,civ_gc,0,0);
    XSetClipMask(display,civ_gc,s->mask);
    XCopyArea(display, s->pixmap, pm, civ_gc,
	      0,0, s->width,s->height, 0,0 );
    XSetClipMask(display,civ_gc,None);
  }

  return(pm);
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
