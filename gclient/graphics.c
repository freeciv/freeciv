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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <gtk/gtk.h>

#include <log.h>
#include <climisc.h>
#include <shared.h>
#include <graphics.h>
#include <unit.h>
#include <game.h>
#include <colors.h>

extern GtkWidget *	drawing;
extern GtkWidget *	toplevel;
extern int 		use_solid_color_behind_units;

#define FLAG_TILES       14*20

SPRITE *		intro_gfx_sprite;
SPRITE *		radar_gfx_sprite;
SPRITE **		tile_sprites;

int			NORMAL_TILE_WIDTH;
int			NORMAL_TILE_HEIGHT;
int			UNIT_TILES;

extern GdkFont *	main_font;

extern GdkGC *		civ_gc;

/***************************************************************************
...
***************************************************************************/
SPRITE *get_tile_sprite( int tileno )
{
    return tile_sprites[tileno];
}


/***************************************************************************
...
***************************************************************************/
void load_intro_gfx( void )
{
    int  w;
    char s	[64];

    intro_gfx_sprite = load_xpmfile( datafilename( "intro.xpm" ) );
    radar_gfx_sprite = load_xpmfile( datafilename( "radar.xpm" ) );

    w = gdk_string_width( main_font, "version" );

    gdk_draw_string( radar_gfx_sprite->pixmap, main_font,
			toplevel->style->white_gc, 160/2-w/2, 40, "version" );

    sprintf( s, "%d.%d.%d", MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION );

    w = gdk_string_width( main_font, s );

    gdk_draw_string( radar_gfx_sprite->pixmap, main_font,
			toplevel->style->white_gc, 160/2-w/2, 60, s );
    return;
}


/***************************************************************************
...
***************************************************************************/
void load_tile_gfx( void )
{
    int     i, x, y, ntiles, a;
    SPRITE *big_sprite, *small_sprite, *unit_sprite, *treaty_sprite;

    big_sprite		= load_xpmfile( datafilename( "tiles.xpm" ) );
    unit_sprite		= load_xpmfile( datafilename( "units.xpm" ) );
    small_sprite	= load_xpmfile( datafilename( "small.xpm" ) );
    treaty_sprite	= load_xpmfile( datafilename( "treaty.xpm" ) );

    ntiles = (20*21) + (20*3) + (31*1) + 3;

    if ( !( tile_sprites = malloc( ntiles*sizeof( SPRITE * ) ) ) )
    {
	log_string( LOG_FATAL, "couldn't malloc tile_sprites array" );
	gtk_main_quit();
    }

    NORMAL_TILE_WIDTH	= big_sprite->width/20;
    NORMAL_TILE_HEIGHT	= big_sprite->height/21;
  
    i = 0;
    for ( y = 0, a = 0; a < 21 && y < big_sprite->height;
						a++, y += NORMAL_TILE_HEIGHT )
	for ( x = 0; x < big_sprite->width; x += NORMAL_TILE_WIDTH )
	{
	    GdkPixmap *mypixmap, *mask;
	    GdkColor   mask_pattern;
	    GdkGC     *gc;

	    mypixmap = gdk_pixmap_new( toplevel->window, NORMAL_TILE_WIDTH,
							NORMAL_TILE_HEIGHT, -1 );

	    gdk_draw_pixmap( mypixmap, civ_gc, big_sprite->pixmap, x, y, 0, 0,
		NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT );

	    mask = gdk_pixmap_new( toplevel->window, NORMAL_TILE_WIDTH,
						    NORMAL_TILE_HEIGHT,  1 );
	    gc = gdk_gc_new( mask );

	    mask_pattern.pixel = 0;
	    gdk_gc_set_foreground( gc, &mask_pattern );
	    gdk_draw_rectangle( mask, gc, TRUE, 0, 0, -1, -1 );
                  
	    mask_pattern.pixel = 1;
	    gdk_gc_set_foreground( gc, &mask_pattern );

	    gdk_draw_pixmap( mask, gc, big_sprite->mask, x, y, 0, 0,
		NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT );
  
	    tile_sprites[i++] = ctor_sprite_mask( mypixmap, mask,
				NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT );
	    if ( mask )
		gdk_gc_destroy( gc );
	}

    for ( x = 0, y = 0; x < small_sprite->width; x += SMALL_TILE_WIDTH )
    {
	GdkPixmap *mypixmap;

	mypixmap = gdk_pixmap_new( toplevel->window, SMALL_TILE_WIDTH,
						    SMALL_TILE_HEIGHT, -1 );

	gdk_draw_pixmap( mypixmap, civ_gc, small_sprite->pixmap, x, y, 0, 0,
				SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT );

	tile_sprites[i++] = ctor_sprite( mypixmap,
				SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT );
    }
  
    {
	GdkPixmap *mypixmap;
  
	mypixmap = gdk_pixmap_new( toplevel->window, 30, 30, -1 );
	gdk_draw_pixmap( mypixmap, civ_gc, treaty_sprite->pixmap, 0, 0, 0, 0, 30, 30 );
	tile_sprites[i++] = ctor_sprite( mypixmap, 30, 30 );
	mypixmap = gdk_pixmap_new( toplevel->window, 30, 30, -1 );
	gdk_draw_pixmap( mypixmap, civ_gc, treaty_sprite->pixmap, 30, 0, 0, 0, 30, 30 );
	tile_sprites[i++] = ctor_sprite( mypixmap, 30, 30 );
    }
  
    UNIT_TILES = i;
    for ( y = 0; y < unit_sprite->height; y += NORMAL_TILE_HEIGHT )
	for ( x = 0; x < unit_sprite->width; x += NORMAL_TILE_WIDTH )
	{
	    GdkPixmap *mypixmap, *mask;
	    GdkColor   mask_pattern;
	    GdkGC     *gc;
        
	    mypixmap = gdk_pixmap_new( toplevel->window, NORMAL_TILE_WIDTH,
							NORMAL_TILE_HEIGHT, -1 );

	    gdk_draw_pixmap( mypixmap, civ_gc, unit_sprite->pixmap, x, y, 0, 0,
		NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT );

	    mask = gdk_pixmap_new( toplevel->window, NORMAL_TILE_WIDTH,
						    NORMAL_TILE_HEIGHT,  1 );
	    gc = gdk_gc_new( mask );

	    mask_pattern.pixel = 0;
	    gdk_gc_set_foreground( gc, &mask_pattern );
	    gdk_draw_rectangle( mask, gc, TRUE, 0, 0, -1, -1 );
                  
	    mask_pattern.pixel = 1;
	    gdk_gc_set_foreground( gc, &mask_pattern );

	    gdk_draw_pixmap( mask, gc, unit_sprite->mask, x, y, 0, 0,
		NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT );
  
	    tile_sprites[i++] = ctor_sprite_mask( mypixmap, mask,
				NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT );
	    if ( mask )
		gdk_gc_destroy( gc );
	}

    free_sprite( unit_sprite );
    free_sprite( big_sprite );
    free_sprite( small_sprite );
    free_sprite( treaty_sprite );
    return;
}


/***************************************************************************
...
***************************************************************************/
SPRITE *ctor_sprite( GdkPixmap *mypixmap, int width, int height )
{
    SPRITE *mysprite = malloc( sizeof( SPRITE ) );

    mysprite->pixmap	= mypixmap;
    mysprite->width	= width;
    mysprite->height	= height;
    mysprite->has_mask	= 0;

    return mysprite;
}

/***************************************************************************
...
***************************************************************************/
SPRITE *ctor_sprite_mask( GdkPixmap *mypixmap, GdkPixmap *mask, 
							int width, int height )
{
    SPRITE *mysprite = malloc( sizeof( SPRITE ) );

    mysprite->pixmap	= mypixmap;
    mysprite->mask	= mask;

    mysprite->width	= width;
    mysprite->height	= height;
    mysprite->has_mask	= 1;

    return mysprite;
}




/***************************************************************************
...
***************************************************************************/
void dtor_sprite( SPRITE *mysprite )
{
    free_sprite( mysprite );
    return;
}



/***************************************************************************
...
***************************************************************************/
SPRITE *load_xpmfile( char *filename )
{
    GdkPixmap      *pixmap;
    GdkBitmap      *mask;
    SPRITE         *mysprite;
    int             width, height;

    if ( !( pixmap = gdk_pixmap_create_from_xpm( toplevel->window,
		&mask, &toplevel->style->bg[GTK_STATE_NORMAL], filename ) ) )
    {
	log_string( LOG_FATAL, "Failed reading XPM file: %s", filename );
	gtk_main_quit();
    }

    if ( !( mysprite = (SPRITE *)malloc( sizeof( SPRITE ) ) ) )
    {
	log_string( LOG_FATAL, "failed malloc sprite struct for %s", filename );
	gtk_main_quit();
    }

    gdk_window_get_size( pixmap, &width, &height );

    fprintf( stderr, "Loaded '%s' %dx%d\n", filename, width, height );

    mysprite->pixmap	= pixmap;
    mysprite->mask	= mask;
    mysprite->has_mask	= ( mask != NULL );
    mysprite->width	= width;
    mysprite->height	= height;

    return mysprite;
}

/***************************************************************************
   Deletes a sprite.  These things can use a lot of memory.
***************************************************************************/
void free_sprite( SPRITE *s )
{
    if ( s->pixmap )
	gdk_pixmap_unref( s->pixmap );
    if ( s->has_mask )
	gdk_bitmap_unref( s->mask );

    free( s );
    return;
}

/***************************************************************************
 ...
***************************************************************************/
GdkPixmap *create_overlay_unit( int i )
{
    GdkPixmap *pm;
    SPRITE    *s = get_tile_sprite( get_unit_type(i)->graphics+UNIT_TILES );
  
    pm = gdk_pixmap_new( toplevel->window, NORMAL_TILE_WIDTH,
							NORMAL_TILE_HEIGHT, -1 );
    if ( use_solid_color_behind_units )
    {
	GdkGC *fill_bg_gc;
	
	fill_bg_gc = gdk_gc_new( pm );

	gdk_gc_set_foreground( fill_bg_gc, &colors_standard[game.player_ptr->race+COLOR_STD_RACE0] );
	gdk_draw_rectangle( pm, fill_bg_gc, TRUE, 0, 0,
			  NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT );

	if ( pm )
	    gdk_gc_destroy( fill_bg_gc );
    }
    else
    {
	SPRITE *flag = get_tile_sprite( game.player_ptr->race+FLAG_TILES );

	gdk_draw_pixmap( pm, civ_gc, flag->pixmap, 0, 0, 0, 0,
						flag->width, flag->height );
    }

    gdk_gc_set_clip_origin( civ_gc, 0, 0 );
    gdk_gc_set_clip_mask( civ_gc, s->mask );

    gdk_draw_pixmap( pm, civ_gc, s->pixmap, 0, 0, 0, 0, s->width, s->height );
    gdk_gc_set_clip_mask( civ_gc, NULL );
    return pm;
}
