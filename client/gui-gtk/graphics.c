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

#include <gtk/gtk.h>

#include <gdk_imlib.h>

#include <log.h>
#include <climisc.h>
#include <shared.h>
#include <graphics.h>
#include <unit.h>
#include <game.h>
#include <colors.h>
#include <mem.h>

#include <goto_cursor.xbm>
#include <goto_cursor_mask.xbm>

extern GtkWidget *	drawing;
extern GtkWidget *	toplevel;
extern GdkWindow *	root_window;
extern int 		use_solid_color_behind_units;

SPRITE **		tile_sprites;
SPRITE *		intro_gfx_sprite;
SPRITE *		radar_gfx_sprite;

int			UNIT_TILES, SPACE_TILES, FLAG_TILES;
int			NORMAL_TILE_WIDTH;
int			NORMAL_TILE_HEIGHT;

/* jjm@codewell.com 30dec1998a
   Moved road and rail tiles to roads.xpm; added tiles for diagonals.
*/
int ROAD_TILES;
int RAIL_TILES;

GdkCursor *		goto_cursor;

extern GdkFont *	main_font;

extern GdkGC *		civ_gc;
extern GdkGC *		fill_bg_gc;

extern GdkGC *		mask_fg_gc;
extern GdkGC *		mask_bg_gc;
extern GdkBitmap *	mask_bitmap;

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

    intro_gfx_sprite = load_xpmfile( datafilename_required( "intro.xpm" ) );
    radar_gfx_sprite = load_xpmfile( datafilename_required( "radar.xpm" ) );

    w = gdk_string_width(main_font, WORD_VERSION);

    gdk_draw_string(radar_gfx_sprite->pixmap, main_font,
			toplevel->style->white_gc, 160/2-w/2, 40, WORD_VERSION);

    sprintf( s, "%d.%d.%d", MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION );

    w = gdk_string_width( main_font, s );

    gdk_draw_string( radar_gfx_sprite->pixmap, main_font,
			toplevel->style->white_gc, 160/2-w/2, 60, s );
    return;
}


/***************************************************************************
return newly allocated sprite cropped from source
***************************************************************************/
struct Sprite *crop_sprite(struct Sprite *source,
			   int x, int y,
			   int width, int height)
{
  GdkPixmap *mypixmap, *mask;

  mypixmap = gdk_pixmap_new(root_window, width, height, -1);

  gdk_draw_pixmap(mypixmap, civ_gc, source->pixmap, x, y, 0, 0,
		  width, height);

  mask=gdk_pixmap_new(mask_bitmap, width, height, 1);
  gdk_draw_rectangle(mask, mask_bg_gc, TRUE, 0, 0, -1, -1 );
  	    
  gdk_draw_pixmap(mask, mask_fg_gc, source->mask, x, y, 0, 0,
		  width, height);
  
  return ctor_sprite_mask(mypixmap, mask, width, height);
}


/***************************************************************************
...
***************************************************************************/
void load_tile_gfx(void)
{
  int i, x, y, ntiles, a;
  SPRITE *big_sprite, *small_sprite, *unit_sprite, *treaty_sprite;
  SPRITE *roads_sprite, *space_sprite, *flags_sprite;
  int row;

  big_sprite   = load_xpmfile(tilefilename("tiles.xpm"));
  unit_sprite  = load_xpmfile(tilefilename("units.xpm"));
  small_sprite = load_xpmfile(tilefilename("small.xpm"));
  treaty_sprite= load_xpmfile(tilefilename("treaty.xpm"));
  roads_sprite = load_xpmfile(tilefilename("roads.xpm"));
  space_sprite = load_xpmfile(tilefilename("space.xpm"));
  flags_sprite = load_xpmfile(tilefilename("flags.xpm"));

  ntiles= (20*19) + (20*3) + (31*1) + 3 + (16*4) + 6 + (14*2);

  tile_sprites=fc_malloc(ntiles*sizeof(SPRITE *));

  NORMAL_TILE_WIDTH=big_sprite->width/20;
  NORMAL_TILE_HEIGHT=big_sprite->height/18;
  
  i=0;
  for(y=0, a=0; a<19 && y<big_sprite->height; a++, y+=NORMAL_TILE_HEIGHT) {
    for(x=0; x<big_sprite->width; x+=NORMAL_TILE_WIDTH) {
      tile_sprites[i++] = crop_sprite(big_sprite, x, y,
				      NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    }
  }

  if(small_sprite->width != SMALL_TILE_WIDTH*31 ||
     small_sprite->height != SMALL_TILE_HEIGHT*1)  {
    freelog(LOG_FATAL, "XPM file small.xpm is the wrong size!");
    freelog(LOG_FATAL, "Expected %dx%d, got %dx%d",
	 SMALL_TILE_WIDTH*31,SMALL_TILE_HEIGHT*1,
	small_sprite->width, small_sprite->height);
    exit(1);
  }
  for(x=0, y=0; x<small_sprite->width; x+=SMALL_TILE_WIDTH) {
    GdkPixmap *mypixmap;

    mypixmap = gdk_pixmap_new( root_window, SMALL_TILE_WIDTH,
  						SMALL_TILE_HEIGHT, -1 );

    gdk_draw_pixmap( mypixmap, civ_gc, small_sprite->pixmap, x, y, 0, 0,
  			    SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT );

    tile_sprites[i++] = ctor_sprite( mypixmap,
  			    SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT );
  }
  
  {
    GdkPixmap *mypixmap;
  
    mypixmap = gdk_pixmap_new( root_window, 30, 30, -1 );
    gdk_draw_pixmap( mypixmap, civ_gc, treaty_sprite->pixmap, 0, 0, 0, 0, 30, 30 );
    tile_sprites[i++] = ctor_sprite( mypixmap, 30, 30 );
    mypixmap = gdk_pixmap_new( root_window, 30, 30, -1 );
    gdk_draw_pixmap( mypixmap, civ_gc, treaty_sprite->pixmap, 30, 0, 0, 0, 30, 30 );
    tile_sprites[i++] = ctor_sprite( mypixmap, 30, 30 );
  }

  if(unit_sprite->width != NORMAL_TILE_WIDTH*20 ||
     unit_sprite->height != NORMAL_TILE_HEIGHT*3)  {
    freelog(LOG_FATAL, "XPM file units.xpm is the wrong size!");
    freelog(LOG_FATAL, "Expected %dx%d, got %dx%d",
	 NORMAL_TILE_WIDTH*20,NORMAL_TILE_HEIGHT*3,
	unit_sprite->width, unit_sprite->height);
    exit(1);
  }

  UNIT_TILES = i;
  for(y=0; y<unit_sprite->height; y+=NORMAL_TILE_HEIGHT) {
    for(x=0; x<unit_sprite->width; x+=NORMAL_TILE_WIDTH) {
      tile_sprites[i++] = crop_sprite(unit_sprite, x, y,
				      NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    }
  }

  if(roads_sprite->width != NORMAL_TILE_WIDTH*16 ||
     roads_sprite->height != NORMAL_TILE_HEIGHT*4)  {
    freelog(LOG_FATAL, "XPM file roads.xpm is the wrong size!");
    freelog(LOG_FATAL, "Expected %dx%d, got %dx%d",
	 NORMAL_TILE_WIDTH*16,NORMAL_TILE_HEIGHT*4,
	roads_sprite->width, roads_sprite->height);
    exit(1);
  }
  /* jjm@codewell.com 30dec1998a */
  for(y=0, row=0; y<roads_sprite->height; y+=NORMAL_TILE_HEIGHT, row++) {
    if (row==0) ROAD_TILES = i;
    if (row==2) RAIL_TILES = i;
    for(x=0; x<roads_sprite->width; x+=NORMAL_TILE_WIDTH) {
      tile_sprites[i++] = crop_sprite(roads_sprite, x, y,
				      NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    }
  }
 
  if (space_sprite->width != space_sprite->height * 6)  {
    freelog(LOG_FATAL, "XPM file space.xpm is the wrong width!");
    freelog(LOG_FATAL, "Expected 6 * height (%d), got %d",
           space_sprite->height * 6, space_sprite->width);
    exit(1);
  }

  SPACE_TILES = i;
  
  for (x = 0; x < space_sprite->width; x += space_sprite->height) {
    tile_sprites[i++] = crop_sprite(space_sprite, x, 0,
				    space_sprite->height,
				    space_sprite->height);
  }

  if(flags_sprite->width != NORMAL_TILE_WIDTH*14 ||
     flags_sprite->height != NORMAL_TILE_HEIGHT*2)  {
    freelog(LOG_FATAL, "XPM file flags.xpm is the wrong size!");
    freelog(LOG_FATAL, "Expected %dx%d, got %dx%d",
	 NORMAL_TILE_WIDTH*14,NORMAL_TILE_HEIGHT*2,
	flags_sprite->width, flags_sprite->height);
    exit(1);
  }

  FLAG_TILES = i;
  for(y=0; y<flags_sprite->height; y+=NORMAL_TILE_HEIGHT) {
    for(x=0; x<flags_sprite->width; x+=NORMAL_TILE_WIDTH) {
      tile_sprites[i++] = crop_sprite(flags_sprite, x, y,
				      NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    }
  }
  
  free_sprite(unit_sprite);
  free_sprite(big_sprite);
  free_sprite(small_sprite);
  free_sprite(treaty_sprite);
  free_sprite(roads_sprite);
  free_sprite(space_sprite);
  free_sprite(flags_sprite);
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
}

/***************************************************************************
...
***************************************************************************/
SPRITE *ctor_sprite( GdkPixmap *mypixmap, int width, int height )
{
    SPRITE *mysprite = fc_malloc(sizeof(SPRITE));

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
    SPRITE *mysprite = fc_malloc(sizeof(SPRITE));

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
struct Sprite *load_xpmfile(char *filename)
{
  GdkBitmap	*m;
  GdkImlibImage *im;
  SPRITE	*mysprite;
  int		 w, h;

  if(!(im=gdk_imlib_load_image(filename))) {
    freelog(LOG_FATAL, "Failed reading XPM file: %s", filename);
    exit(1);
  }

  mysprite=fc_malloc(sizeof(struct Sprite));

  w=im->rgb_width; h=im->rgb_height;

  if(!gdk_imlib_render (im, w, h)) {
    freelog(LOG_FATAL, "failed render of sprite struct for %s", filename);
    exit(1);
  }
  
  mysprite->pixmap    = gdk_imlib_move_image (im);
  m		      = gdk_imlib_move_mask  (im);
  mysprite->mask      = m;
  mysprite->has_mask  = (m != NULL);
  mysprite->width     = w;
  mysprite->height    = h;

  gdk_imlib_destroy_image (im);

  return mysprite;
}

/***************************************************************************
   Deletes a sprite.  These things can use a lot of memory.
***************************************************************************/
void free_sprite(SPRITE *s)
{
  gdk_imlib_free_pixmap(s->pixmap);
  free(s);
  return;
}

/***************************************************************************
 ...
***************************************************************************/
GdkPixmap *create_overlay_unit(int i)
{
  GdkPixmap *pm;
  int bg_color;
  
  pm=gdk_pixmap_new(root_window, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT, -1);

  /* Give tile a background color, based on the type of unit */
  bg_color = COLOR_STD_RACE0+game.player_ptr->race;
  switch (get_unit_type(i)->move_type) {
    case LAND_MOVING: bg_color = COLOR_STD_GROUND; break;
    case SEA_MOVING:  bg_color = COLOR_STD_OCEAN;  break;
    case HELI_MOVING: bg_color = COLOR_STD_YELLOW; break;
    case AIR_MOVING:  bg_color = COLOR_STD_CYAN;   break;
    default:	      bg_color = COLOR_STD_BLACK;  break;
  }
  gdk_gc_set_foreground(fill_bg_gc, colors_standard[bg_color]);
  gdk_draw_rectangle(pm, fill_bg_gc, TRUE, 0, 0,
        	     NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);

  /* If we're using flags, put one on the tile */
  if(!use_solid_color_behind_units)  {
    struct Sprite *flag=get_tile_sprite(game.player_ptr->race + FLAG_TILES);

    gdk_gc_set_clip_origin(civ_gc, 0, 0);
    gdk_gc_set_clip_mask(civ_gc, flag->mask);

    gdk_draw_pixmap(pm, civ_gc, flag->pixmap, 0, 0, 0, 0,
					flag->width, flag->height);
    gdk_gc_set_clip_mask(civ_gc, NULL);
  }

  /* Finally, put a picture of the unit in the tile */
  if(i<U_LAST) {
    struct Sprite *s=get_tile_sprite(get_unit_type(i)->graphics+UNIT_TILES);

    gdk_gc_set_clip_origin(civ_gc, 0, 0);
    gdk_gc_set_clip_mask(civ_gc, s->mask);

    gdk_draw_pixmap(pm, civ_gc, s->pixmap, 0, 0, 0, 0, s->width, s->height);
    gdk_gc_set_clip_mask(civ_gc, NULL);
  }

  return pm;
}
