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

#ifdef __cplusplus
extern "C" {
#endif
#include "game.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "unit.h"
#include "version.h"

#include "options.h"
#include "climisc.h"
#include "tilespec.h"

#include "mapview_g.h"
#ifdef __cplusplus
}
#endif


#include <TranslationKit.h>
#include <storage/File.h>
#include <interface/Rect.h>
#include <interface/View.h>
#include <support/Debug.h>
#include "graphics.hpp"
#include "colors.hpp"


#include "goto_cursor.xbm"
#include "goto_cursor_mask.xbm"

Sprite *		intro_gfx_sprite;
Sprite *		radar_gfx_sprite;

unsigned char goto_cursor[68];


/***************************************************************************
...
***************************************************************************/

SpriteRep::SpriteRep( BBitmap *bm )
	: pixels(bm), useCount(0)
{
}

SpriteRep::SpriteRep( const char *filename )
	: pixels(NULL), useCount(0)
{
	BFile file(filename, B_READ_ONLY); 
    BTranslatorRoster *roster = BTranslatorRoster::Default(); 
    BBitmapStream stream; 
    if (roster->Translate(&file, NULL, NULL, &stream, B_TRANSLATOR_BITMAP) 
				== B_OK)  {
    	stream.DetachBitmap(&pixels);
	}
}

SpriteRep::~SpriteRep()
{
	delete pixels;
	pixels = NULL;
}

/***************************************************************************
...
***************************************************************************/

Sprite::Sprite( const char *xpmfile )
{
	rep = new SpriteRep(xpmfile);
	rep->Using();
	cropTo = rep->Bounds();
}

Sprite::Sprite( BBitmap *bm )
{
	rep = new SpriteRep(bm);
	rep->Using();
	cropTo = bm->Bounds();
}

Sprite::Sprite( const Sprite &s )
{
	rep = s.rep;
	rep->Using();
	cropTo = s.cropTo;
}

Sprite::Sprite( const Sprite &s, BRect crop )
{
	rep = s.rep;
	rep->Using();
	cropTo = crop;
}

Sprite::~Sprite()
{
	rep->Freeing();
	rep = NULL;
}


/***************************************************************************
...
***************************************************************************/
void load_intro_gfx( void )
{
    intro_gfx_sprite = load_gfxfile(main_intro_filename);
    radar_gfx_sprite = load_gfxfile(minimap_intro_filename);
}


/***************************************************************************
return newly allocated sprite cropped from source
***************************************************************************/
struct Sprite *crop_sprite(struct Sprite *source,
			   int x, int y,
			   int width, int height)
{
	BRect r( x, y, x+width-1, y+height-1 );
	return new Sprite( *source, r );
}


/***************************************************************************
...
***************************************************************************/
void load_cursors(void)
{
	goto_cursor[0] = goto_cursor_height;
	goto_cursor[1] = 1;
	goto_cursor[2] = goto_cursor_y_hot;
	goto_cursor[3] = goto_cursor_x_hot;

	memcpy( &goto_cursor[ 4], goto_cursor_bits,      32 );
	memcpy( &goto_cursor[36], goto_cursor_mask_bits, 32 );
}


/***************************************************************************
...
***************************************************************************/
Sprite *load_gfxfile(const char *filename)
{
	return new Sprite(filename);
}


/***************************************************************************
   Deletes a sprite.  These things can use a lot of memory.
***************************************************************************/
void free_sprite(Sprite *s)
{
	delete s;
}

/***************************************************************************
 ... BBitmap handed back was new'ed
***************************************************************************/
Sprite *create_overlay_unit(int i)
{
	/* Give tile a background color, based on the type of unit */
	int bg_color;
	switch (get_unit_type(i)->move_type) {
    	case LAND_MOVING: bg_color = COLOR_STD_GROUND; break;
    	case SEA_MOVING:  bg_color = COLOR_STD_OCEAN;  break;
    	case HELI_MOVING: bg_color = COLOR_STD_YELLOW; break;
    	case AIR_MOVING:  bg_color = COLOR_STD_CYAN;   break;
    	default:	      bg_color = COLOR_STD_BLACK;  break;
  	}

	BRect r(0, 0, NORMAL_TILE_WIDTH-1, NORMAL_TILE_HEIGHT-1 );
	BBitmap *pm = new BBitmap( r, B_RGB_32_BIT, true );
	if ( !pm || ! pm->Lock() ) {
		delete pm;
		return NULL;
	}

	BView *v = new BView( r, (char*)NULL, B_FOLLOW_NONE, 0 );
	pm->AddChild( v );
	pm->ChildAt(0)->SetDrawingMode( B_OP_OVER );
	pm->ChildAt(0)->SetHighColor( colors_standard[bg_color] );
	pm->ChildAt(0)->StrokeRect( r );

	/* If we're using flags, put one on the tile */
	if(!solid_color_behind_units)  {
    	Sprite *flag=get_nation_by_plr(game.player_ptr)->flag_sprite;
		r = flag->Bounds();
		pm->ChildAt(0)->DrawBitmap( flag->Pixels(), r, r );
	}

  	/* Finally, put a picture of the unit in the tile */
  	if(i<game.num_unit_types) {
		Sprite *s=get_unit_type(i)->sprite;
		r = s->Bounds();
		pm->ChildAt(0)->DrawBitmap( s->Pixels(), r, r );
  	}

	pm->RemoveChild( v );
	pm->Unlock();
	delete v;
	v = NULL;

  	return new Sprite(pm);
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


char **
gfx_fileextensions(void)
{
  static char *ext[] =
  {
    "xpm",
    NULL
  };

  return ext; 
}
