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

extern struct Sprite *intro_gfx_sprite;
extern struct Sprite *radar_gfx_sprite;

HBITMAP BITMAP2HBITMAP(BITMAP *bmp);
HBITMAP getcachehbitmap(BITMAP *bmp, int *cache_id);
BITMAP premultiply_alpha(BITMAP bmp);
BITMAP generate_mask(BITMAP bmp);

#endif  /* FC__GRAPHICS_H */
