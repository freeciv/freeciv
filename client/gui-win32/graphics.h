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

#include "graphics_g.h"

typedef struct Sprite SPRITE;
struct Sprite
{
  int has_mask;
  BITMAP bmp;
  BITMAP mask;
  int width;
  int height;
};
void draw_sprite(struct Sprite *sprite,HDC hdc,int x, int y);
void draw_sprite_part(struct Sprite *sprite,HDC hdc,
		       int x, int y, int w, int h,int xsrc,int ysrc);
void draw_sprite_part_with_mask(struct Sprite *sprite,
				struct Sprite *sprite_mask,
				HDC hdc,
				int x, int y, int w, int h,
				int xsrc, int ysrc);
void init_fog_bmp(void);
void draw_fog_part(HDC hdc,int x, int y,int w, int h,
		   int xsrc, int ysrc);

SPRITE *get_citizen_sprite(int frame);
extern HBITMAP BITMAP2HBITMAP(BITMAP *bmp);
extern SPRITE *intro_gfx_sprite;
extern SPRITE *radar_gfx_sprite;


#endif  /* FC__GRAPHICS_H */
