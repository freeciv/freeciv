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

struct Sprite
{
  int width;
  int height;
  int hasmask;

  int offx,offy;
  int reference;
  int numsprites; /* contains the number of references */

  void *bitmap;
  void *mask;
  void *picture;

  struct Sprite *parent;
};

struct SpriteNode /* could better embed this in struct Sprite */
{
  struct MinNode node;
  struct Sprite *sprite;
  STRPTR filename;
};

extern APTR pen_shared_map;


extern int get_normal_tile_height(void);
extern int get_normal_tile_width(void);

extern void put_sprite(struct RastPort *rp, struct Sprite *sprite, LONG x, LONG y);
extern struct Sprite *load_sprite(const char *filename, ULONG usemask);
extern int render_sprites(APTR drawhandle);
extern void real_free_sprite(struct Sprite *sprite);
extern void cleanup_sprites(void);
extern void put_tile(struct RastPort *rp, int destx, int desty, int x, int y, int abs_x0, int abs_y0, int citymode);
extern void put_unit_tile(struct RastPort *rp, struct unit *punit, int x1, int y1);
extern void put_sprite_overlay(struct RastPort *rp, struct Sprite *sprite, LONG x, LONG y);
extern void put_sprite_overlay_height(struct RastPort *rp, struct Sprite *sprite, LONG x, LONG y, LONG height);
extern void put_city_output_tile(struct RastPort *rp,
       int food, int shield, int trade, int offx, int offy, int x_tile, int y_tile);

extern struct Sprite *get_citizen_sprite(int frame);
extern struct Sprite *get_thumb_sprite(int onoff);

extern int load_all_sprites(void);
extern void free_all_sprites(void);

#endif  /* FC__GRAPHICS_H */
