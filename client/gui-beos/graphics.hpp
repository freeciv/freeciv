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
#ifndef FC__GRAPHICS_HPP
#define FC__GRAPHICS_HPP

#ifdef __cplusplus
extern "C" {
#endif

#include "graphics_g.h"

#ifdef __cplusplus
}
#endif

#include <Bitmap.h>

class SpriteRep {
	BBitmap *pixels;
	int		useCount;
public:
	SpriteRep( BBitmap *bm );
	SpriteRep( const char *xpmfile );
	~SpriteRep();
public:
	BBitmap*	Pixels(void) 	{ return pixels; }
	bool	InitCheck(void) { return (pixels != NULL); }
	void	Freeing(void)	{ if (--useCount < 1) delete this; }
	void	Using(void)		{ useCount++; }
	BRect	Bounds(void)
		{ return (pixels ? pixels->Bounds() : BRect(0,0,0,0)); }
};

class Sprite {
	struct SpriteRep *	rep;
	BRect				cropTo;
public:
	Sprite( const char *xpmfile );
	Sprite( BBitmap *bm );
	Sprite( const Sprite& s );
	Sprite( const Sprite& s, BRect crop );
	~Sprite();
public:
	BBitmap*	Pixels(void) { return rep->Pixels(); }
	BRect		Bounds(void) { return cropTo; }
	int			Width( void) { return cropTo.IntegerWidth();  }
	int			Height(void) { return cropTo.IntegerHeight(); }
	bool		InitCheck(void) { return (rep != NULL && rep->InitCheck()); }
};

Sprite*	create_overlay_unit	(int i);
void load_intro_gfx(void);

extern unsigned char goto_cursor[];

extern Sprite *intro_gfx_sprite;
extern Sprite *radar_gfx_sprite;

#endif	/* FC__GRAPHICS_HPP */
