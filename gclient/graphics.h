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
#ifndef __GRAPHICS_H
#define __GRAPHICS_H

#include <gtk/gtk.h>

/* These #defines are the size of the tiles used within the game.
 * Tiles for the units and city squares, etc, are usually 30x30.
 * Tiles for things like food production, etc, are usually 15x20.  We
 * say "usually" for two reasons:  
 *
 * First, it is feasible to replace the tiles in the .xpm files with
 * ones of some other size.  Mitch Davis (mjd@alphalink.com.au) has
 * done this, and replaced all the tiles with the ones from the
 * original Civ.  The tiles from the original civ are 32x32.  All that
 * is required is that these constants be changed.
 *
 * Second, although there is currently no "zoom" feature as in the
 * original Civ, we might add it some time in the future.  If and when
 * this happens, we'll have to stop using the constants, and go back
 * to using ints which change at runtime.  Note, this would require
 * quite a bit of memory and pixmap management work, so it seems like
 * a nasty task.
 *
 * BUG: pjunold informs me that there are hard-coded geometries in
 * the Freeciv.h file which will prevent ideal displaying of pixmaps
 * which are not of the original 30x30 size.  Also, the pixcomm widget
 * apparently also does not handle this well.  Truthfully, I hadn't
 * noticed at all! :-) (mjd)
 */

 /*
#define NORMAL_TILE_WIDTH  30
#define NORMAL_TILE_HEIGHT 30
 */
extern int NORMAL_TILE_WIDTH;
extern int NORMAL_TILE_HEIGHT;

#define SMALL_TILE_WIDTH   15
#define SMALL_TILE_HEIGHT  20


typedef struct	sprite			SPRITE;

struct sprite
{
    GdkPixmap	*pixmap;
    GdkBitmap	*mask;
    int		 has_mask;
    int 	 width;
    int 	 height;
};


SPRITE *	ctor_sprite		( GdkPixmap *mypixmap, int width, int height );
SPRITE *	ctor_sprite_mask	( GdkPixmap *mypixmap, GdkPixmap *mask,
					int width, int height );

SPRITE *	load_xpmfile		( char *filename );
void		free_sprite		( SPRITE *s );

void		dtor_sprite		( SPRITE *mysprite );

SPRITE *	get_tile_sprite		( int tileno );
void		load_tile_gfx		( void );
void		load_intro_gfx		( void );

SPRITE *	load_gfxfile		( char *filename, int makemask );

GdkPixmap *	create_overlay_unit	( int i );
#endif


