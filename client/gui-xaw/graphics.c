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
#include <X11/xpm.h>

#include <shared.h>
#include <log.h>
#include <unit.h>
#include <game.h>
#include <graphics.h>
#include <colors.h>
#include <climisc.h>
#include <mem.h>

#include <goto_cursor.xbm>
#include <goto_cursor_mask.xbm>

extern int display_depth;
extern Widget map_canvas;
extern Display *display;
extern GC fill_bg_gc;
extern GC civ_gc, font_gc;
extern Colormap cmap;
extern Widget toplevel;
extern Window root_window;
extern XFontStruct *main_font_struct;
extern int use_solid_color_behind_units;

struct Sprite **tile_sprites;
struct Sprite *intro_gfx_sprite;
struct Sprite *radar_gfx_sprite;
int UNIT_TILES, SPACE_TILES, FLAG_TILES;
int NORMAL_TILE_WIDTH;
int NORMAL_TILE_HEIGHT;

/* jjm@codewell.com 30dec1998a
   Moved road and rail tiles to roads.xpm; added tiles for diagonals.
*/
int ROAD_TILES;
int RAIL_TILES;

Cursor goto_cursor;

/***************************************************************************
...
***************************************************************************/
struct Sprite *get_tile_sprite(int tileno)
{
  return tile_sprites[tileno];
}


/***************************************************************************
...
***************************************************************************/
void load_intro_gfx(void)
{
  int w;
  char s[64];

  intro_gfx_sprite=load_xpmfile(datafilename("intro.xpm"));
  radar_gfx_sprite=load_xpmfile(datafilename("radar.xpm"));

  w=XTextWidth(main_font_struct, WORD_VERSION, strlen(WORD_VERSION));
	
  XDrawString(display, radar_gfx_sprite->pixmap, font_gc, 
	      160/2-w/2, 40, 
	      WORD_VERSION, strlen(WORD_VERSION));
  
  sprintf(s, "%d.%d.%d", MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION);
  w=XTextWidth(main_font_struct, s, strlen(s));
  XDrawString(display, radar_gfx_sprite->pixmap, font_gc, 
	      160/2-w/2, 60, s, strlen(s));
}


/***************************************************************************
return newly allocated sprite cropped from source
***************************************************************************/
struct Sprite *crop_sprite(struct Sprite *source,
			   int x, int y,
			   int width, int height)
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

/* This stores the index so far in global tile_sprites
   between the end of load_tile_gfx_first() and the start
   of load_tile_gfx_rest():
*/
static int tiles_i;

/***************************************************************************
  Load tiles.xpm and small.xpm; these are necessary before we can
  call setup_widgets.  Note that these two files must _not_ exhaust
  the colormap (on 256 color systems) or setup_widgets will likely
  dump core.
  Initialises globals:
     tile_sprites, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT.
  Fills in tile_sprites for tiles.xpm and small.xpm graphics.   
***************************************************************************/
void load_tile_gfx_first(void)
{
  int i, x, y, ntiles, a;
  struct Sprite *big_sprite, *small_sprite;

  big_sprite   = load_xpmfile(tilefilename("tiles.xpm"));
  small_sprite = load_xpmfile(tilefilename("small.xpm"));

  /* tiles + units + small + treaty + roads + space + flags: */
  ntiles= (20*19) + (20*3) + (31*1) + 3 + (16*4) + 6 + (14*2);

  tile_sprites=fc_malloc(ntiles*sizeof(struct Sprite *));

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
    Pixmap mypixmap;
    
    mypixmap=XCreatePixmap(display, root_window, 
			   SMALL_TILE_WIDTH, 
			   SMALL_TILE_HEIGHT, 
			   display_depth);
    XCopyArea(display, small_sprite->pixmap, mypixmap, civ_gc, 
	      x, y, SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT, 0 ,0);
    
    tile_sprites[i++]=ctor_sprite(mypixmap, 
				  SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT);
  }
  free_sprite(big_sprite);
  free_sprite(small_sprite);
  tiles_i = i;
}

/***************************************************************************
  Load the rest of the xpm files: those which are not required
  for setup_widgets().
***************************************************************************/
void load_tile_gfx_rest(void)
{
  int i = tiles_i;
  int x, y;
  struct Sprite *unit_sprite, *treaty_sprite;
  struct Sprite *roads_sprite, *space_sprite, *flags_sprite;
  int row;

  unit_sprite  = load_xpmfile(tilefilename("units.xpm"));
  treaty_sprite= load_xpmfile(tilefilename("treaty.xpm"));
  roads_sprite = load_xpmfile(tilefilename("roads.xpm"));
  space_sprite = load_xpmfile(tilefilename("space.xpm"));
  flags_sprite = load_xpmfile(tilefilename("flags.xpm"));

  {
    Pixmap mypixmap;

    mypixmap=XCreatePixmap(display, root_window, 30,30, display_depth);
    XCopyArea(display, treaty_sprite->pixmap, mypixmap, civ_gc, 0,0, 30,30, 0,0);
    tile_sprites[i++]=ctor_sprite(mypixmap, 30,30);
    mypixmap=XCreatePixmap(display, root_window, 30,30, display_depth);
    XCopyArea(display, treaty_sprite->pixmap, mypixmap, civ_gc, 30,0, 30,30, 0,0);
    tile_sprites[i++]=ctor_sprite(mypixmap, 30,30);
/*  mypixmap=XCreatePixmap(display, root_window, 300,30, display_depth);
    XCopyArea(display, treaty_sprite->pixmap, mypixmap, civ_gc, 60,0, 300,30, 0,0);
    tile_sprites[i++]=ctor_sprite(mypixmap, 300,30); */
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
  Pixmap pixmap, mask;
  XColor white, black;

  white.pixel = colors_standard[COLOR_STD_WHITE];
  black.pixel = colors_standard[COLOR_STD_BLACK];
  XQueryColor(display, cmap, &white);
  XQueryColor(display, cmap, &black);

  pixmap = XCreateBitmapFromData(display, root_window, goto_cursor_bits,
				 goto_cursor_width, goto_cursor_height);
  mask   = XCreateBitmapFromData(display, root_window, goto_cursor_mask_bits,
				 goto_cursor_mask_width, goto_cursor_mask_height);

  goto_cursor = XCreatePixmapCursor(display, pixmap, mask,
				    &white, &black,
				    goto_cursor_x_hot, goto_cursor_y_hot);

  XFreePixmap(display, pixmap);
  XFreePixmap(display, mask);
}

/***************************************************************************
...
***************************************************************************/
struct Sprite *ctor_sprite(Pixmap mypixmap, int width, int height)
{
  struct Sprite *mysprite=fc_malloc(sizeof(struct Sprite));
  mysprite->pixmap=mypixmap;
  mysprite->width=width;
  mysprite->height=height;
  mysprite->has_mask=0;
  return mysprite;
}

/***************************************************************************
...
***************************************************************************/
struct Sprite *ctor_sprite_mask(Pixmap mypixmap, Pixmap mask, 
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



/***************************************************************************
...
***************************************************************************/
struct Sprite *load_xpmfile(char *filename)
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
  
  if((err=XpmReadFileToPixmap(display, root_window, filename, &mypixmap, 
			      &mask_bitmap, &attributes))!=XpmSuccess) {
    if(err==XpmColorError || err==XpmColorFailed) {
      color_error();
      goto again;
    }
    
    freelog(LOG_FATAL, "Failed reading XPM file: %s", filename);
    freelog(LOG_FATAL, "The environment variable FREECIV_DATADIR is '%s'",
	getenv("FREECIV_DATADIR") ? getenv("FREECIV_DATADIR") : "");
    freelog(LOG_FATAL, "Check if you got read permissions to the file");
    exit(1);
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
  int bg_color;
  
  pm=XCreatePixmap(display, root_window, 
		   NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT, display_depth);

  /* Give tile a background color, based on the type of unit */
  bg_color = COLOR_STD_RACE0+game.player_ptr->race;
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
  if(!use_solid_color_behind_units)  {
    struct Sprite *flag=get_tile_sprite(game.player_ptr->race + FLAG_TILES);

    XSetClipOrigin(display, civ_gc, 0,0);
    XSetClipMask(display, civ_gc, flag->mask);
    XCopyArea(display, flag->pixmap, pm, civ_gc, 0,0, 
    	      flag->width,flag->height, 0,0);
    XSetClipMask(display, civ_gc, None);
  }

  /* Finally, put a picture of the unit in the tile */
  if(i<U_LAST) {
    struct Sprite *s=get_tile_sprite(get_unit_type(i)->graphics+UNIT_TILES);

    XSetClipOrigin(display,civ_gc,0,0);
    XSetClipMask(display,civ_gc,s->mask);
    XCopyArea(display, s->pixmap, pm, civ_gc,
	      0,0, s->width,s->height, 0,0 );
    XSetClipMask(display,civ_gc,None);
  }

  return(pm);
}
