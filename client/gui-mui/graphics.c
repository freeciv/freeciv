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

#include <guigfx/guigfx.h>
#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/utility.h>
#include <proto/guigfx.h>

#include "fcintl.h"
#include "options.h"

#include "control.h"

#include "graphics.h"
#include "muistuff.h"
#include "tilespec.h"

APTR pen_shared_map;

/* Own sprite list - used so we can reload the sprites if needed */
static struct MinList sprite_list;
static LONG sprite_initialized;

/***************************************************************************
  This function is so that packhand.c can be gui-independent, and
  not have to deal with Sprites itself.
***************************************************************************/
void free_intro_radar_sprites(void)
{
}

void free_sprite(struct Sprite *sprite)
{
}

/****************************************************************
 Return the gfx file extension the client supports
*****************************************************************/
char **gfx_fileextensions(void)
{
  static char *ext[] =
  {
    "png",
    "ilbm",
    "xpm",
    NULL
  };

  return ext;
}

/****************************************************************
 Crop a sprite from a bigger sprite (called from the gui
 independend part)
 Note that the ImageData is not copied but only referenced,
 this saves memory (because Data of BitMaps are aligned) and
 loading time
*****************************************************************/
struct Sprite *crop_sprite(struct Sprite *source, int x, int y, int width, int height)
{
  struct Sprite *sprite = (struct Sprite *) malloc(sizeof(struct Sprite)); /*AllocVec(sizeof(struct Sprite),MEMF_CLEAR);*/

  if (sprite)
  {
    memset(sprite, 0, sizeof(*sprite));

    source->numsprites++;

    sprite->width = width;
    sprite->height = height;
    sprite->offx = x;
    sprite->offy = y;
    sprite->reference = TRUE;
    sprite->parent = source;

    return sprite;
  }
  return NULL;
}

/****************************************************************
 Load a gfxfile as a sprite (called from the gui independend
 part)
*****************************************************************/
struct Sprite *load_gfxfile(const char *filename)
{
  struct SpriteNode *node = (struct SpriteNode *) AllocVec(sizeof(*node), 0x10000);
  if (node)
  {
    struct Sprite *sprite = load_sprite(filename, TRUE);
    if (sprite)
    {
      if (!sprite_initialized)
      {
	NewList((struct List *) &sprite_list);
	sprite_initialized = TRUE;
      }
      node->sprite = sprite;
      node->filename = StrCopy((STRPTR) filename);
      AddTail((struct List *) &sprite_list, (struct Node *) node);
      return sprite;
    }
    FreeVec(node);
  }

  return NULL;
}

/****************************************************************
...
*****************************************************************/
int get_normal_tile_height(void)
{
  return NORMAL_TILE_HEIGHT;
}

/****************************************************************
...
*****************************************************************/
int get_normal_tile_width(void)
{
  return NORMAL_TILE_WIDTH;
}


/****************************************************************
 Allocate and load a sprite
*****************************************************************/
struct Sprite *load_sprite(const char *filename, ULONG usemask)
{
  struct Sprite *sprite = (struct Sprite *) malloc(sizeof(struct Sprite)); /*AllocVec(sizeof(struct Sprite),MEMF_CLEAR);*/

  if (sprite)
  {
    APTR picture;
    memset(sprite, 0, sizeof(*sprite));

    picture = LoadPicture((char *) filename,
			  GGFX_UseMask, usemask,
			  TAG_DONE);
    if (picture)
    {
      if (AddPicture(pen_shared_map, picture,
                     GGFX_Weight, (usemask ? (strstr(filename, "tiles") ? 200 : 10) : (1)),
		     TAG_DONE))
      {
	sprite->picture = picture;

	GetPictureAttrs(picture,
			PICATTR_Width, &sprite->width,
			PICATTR_Height, &sprite->height,
			PICATTR_AlphaPresent, &sprite->hasmask,
			TAG_DONE);

	if (usemask && !sprite->hasmask)
	{
	  printf(_("Could not get the mask although there must be one! Graphics may look corrupt.\n"));
	}

	return sprite;
      }
    }
    free(sprite);
  }
  return NULL;
}

/****************************************************************
 Cleanup the sprite
*****************************************************************/
static void cleanup_sprite(struct Sprite *sprite)
{
  if (!sprite->reference)
  {
    if (sprite->bitmap)
    {
      FreeBitMap((struct BitMap *) sprite->bitmap);
      sprite->bitmap = NULL;
    }
    if (sprite->mask)
    {
      FreeVec(sprite->mask);
      sprite->mask = NULL;
    }
    if (sprite->picture)
    {
      DeletePicture(sprite->picture);
      sprite->picture = NULL;
    }
  }
}

/****************************************************************
 Cleanup all sprites
*****************************************************************/
void cleanup_sprites(void)
{
  struct SpriteNode *node = (struct SpriteNode *) sprite_list.mlh_Head;
  while (node->node.mln_Succ)
  {
    cleanup_sprite(node->sprite);
    node = (struct SpriteNode *) node->node.mln_Succ;
  }
}

/****************************************************************
 Free the sprite
*****************************************************************/
void real_free_sprite(struct Sprite *sprite)
{
  cleanup_sprite(sprite);
  free(sprite);
}

/****************************************************************
 Free all sprites
*****************************************************************/
static void free_sprites(void)
{
  struct SpriteNode *node;
  if(sprite_initialized)
  {
    while ((node = (struct SpriteNode *) RemHead((struct List *) &sprite_list)))
    {
      real_free_sprite(node->sprite);
      if (node->filename)
        FreeVec(node->filename);
      FreeVec(node);
    }
  }
}

/****************************************************************
 Render the sprite for this drawhandle
*****************************************************************/
static int render_sprite(APTR drawhandle, struct SpriteNode *node)
{
  struct Sprite *sprite = node->sprite;

  if (!sprite->picture)
  {
    struct Sprite *ns;

    cleanup_sprite(sprite);

    if ((ns = load_sprite(node->filename, sprite->hasmask)))
    {
      sprite->picture = ns->picture;
      sprite->hasmask = ns->hasmask;
      sprite->width = ns->width;
      sprite->height = ns->height;
      free(ns);
    }
  }


  if (!sprite->picture)
    return FALSE;

  if ((sprite->bitmap = CreatePictureBitMap(drawhandle, sprite->picture,
					    GGFX_DitherMode, DITHERMODE_EDD,
					    TAG_DONE)))
  {
    if (sprite->hasmask)
    {
      ULONG flags = GetBitMapAttr(sprite->bitmap, BMA_FLAGS);
      ULONG mem_flags = MEMF_ANY;
      LONG array_width = GetBitMapAttr(sprite->bitmap, BMA_WIDTH) / 8;
      LONG len;
      UBYTE *data;

      if (flags & BMF_STANDARD)
	mem_flags = MEMF_CHIP;

      len = array_width * sprite->height;

      if ((data = AllocVec(len + 8, mem_flags)))
      {
	sprite->mask = data;

	if (CreatePictureMaskA(sprite->picture, data, array_width /*(((sprite->width+15)>>4)<<1) */ , NULL))
	{
	  DeletePicture(sprite->picture);
	  sprite->picture = NULL;
	  return TRUE;
	}
      }

      sprite->mask = NULL;
    }
    else
    {
      DeletePicture(sprite->picture);
      sprite->picture = NULL;
      return TRUE;
    }
  }
  return FALSE;
}

/****************************************************************
 Render all sprites
*****************************************************************/
int render_sprites(APTR drawhandle)
{
  struct SpriteNode *node = (struct SpriteNode *) sprite_list.mlh_Head;
  while (node->node.mln_Succ)
  {
    if (!render_sprite(drawhandle, node))
      return FALSE;
    node = (struct SpriteNode *) node->node.mln_Succ;
  }
  return TRUE;
}

/****************************************************************
 Returns a cititen sprite
*****************************************************************/
struct Sprite *get_citizen_sprite(int frame)
{
  return sprites.citizen[CLIP(0, frame, NUM_TILES_CITIZEN - 1)];
}

/****************************************************************
 Returns a thumb sprite
*****************************************************************/
struct Sprite *get_thumb_sprite(int onoff)
{
  return sprites.treaty_thumb[BOOL_VAL(onoff)];
}


/****************************************************************
 Preload all sprites (but don't render them)
 Also makes necessary initialisations
*****************************************************************/
int load_all_sprites(void)
{
  if ((pen_shared_map = CreatePenShareMapA(NULL)))
  {
    tilespec_load_tiles();
    return TRUE;
  }
  return FALSE;
}

/****************************************************************
 Free everything related wit the sprites (and sprites itself)
*****************************************************************/
void free_all_sprites(void)
{
  free_sprites();

  if (pen_shared_map)
  {
    DeletePenShareMap(pen_shared_map);
    pen_shared_map = NULL;
  }
}

/****************************************************************
 Draw the whole sprite on position x,y in the Rastport
*****************************************************************/
void put_sprite(struct RastPort *rp, struct Sprite *sprite, LONG x, LONG y)
{
  struct BitMap *bmap;
  if (!sprite)
    return;

  bmap = (struct BitMap *) sprite->bitmap;
  if (!bmap)
    bmap = (struct BitMap *) sprite->parent->bitmap;
  BltBitMapRastPort(bmap, sprite->offx, sprite->offy,
		    rp, x, y, sprite->width, sprite->height, 0xc0);

}

struct LayerHookMsg
{
  struct Layer *layer;
  struct Rectangle bounds;
  LONG offsetx;
  LONG offsety;
};

struct BltMaskHook
{
  struct Hook hook;
  struct BitMap maskBitMap;
  struct BitMap *srcBitMap;
  LONG srcx,srcy;
  LONG destx,desty;
};

static VOID MyBltMaskBitMap( CONST struct BitMap *srcBitMap, LONG xSrc, LONG ySrc, struct BitMap *destBitMap,
LONG xDest, LONG yDest, LONG xSize, LONG ySize, struct BitMap *maskBitMap )
{
  BltBitMap(srcBitMap,xSrc,ySrc,destBitMap, xDest, yDest, xSize, ySize, 0x99,~0,NULL);
  BltBitMap(maskBitMap,xSrc,ySrc,destBitMap, xDest, yDest, xSize, ySize, 0xe2,~0,NULL);
  BltBitMap(srcBitMap,xSrc,ySrc,destBitMap, xDest, yDest, xSize, ySize, 0x99,~0,NULL);
}

HOOKPROTO(HookFunc_BltMask, void, struct RastPort *rp, struct LayerHookMsg *msg)
{
  struct BltMaskHook *h = (struct BltMaskHook*)hook;

  LONG width = msg->bounds.MaxX - msg->bounds.MinX+1;
  LONG height = msg->bounds.MaxY - msg->bounds.MinY+1;
  LONG offsetx = h->srcx + msg->offsetx - h->destx;
  LONG offsety = h->srcy + msg->offsety - h->desty;

  MyBltMaskBitMap( h->srcBitMap, offsetx, offsety, rp->BitMap, msg->bounds.MinX,
  msg->bounds.MinY, width, height, &h->maskBitMap);
}

/****************************************************************
 Draw the sprite on position x,y in the Rastport (masked)
 restricted to height
*****************************************************************/
void put_sprite_overlay_height(struct RastPort *rp, struct Sprite *sprite, LONG x, LONG y, LONG height)
{
  struct BitMap *bmap;
  APTR mask = NULL;
  if (!sprite)
    return;

  if (!(bmap = (struct BitMap *) sprite->bitmap))
  {
    bmap = (struct BitMap *) sprite->parent->bitmap;
    mask = sprite->parent->mask;
  }

  if (mask)
  {
    if (GetBitMapAttr(bmap,BMA_FLAGS)&BMF_INTERLEAVED)
    {
      LONG src_depth = GetBitMapAttr(bmap,BMA_DEPTH);
      struct Rectangle rect;
      struct BltMaskHook hook;

      /* Define the destination rectangle in the rastport */
      rect.MinX = x;
      rect.MinY = y;
      rect.MaxX = x + sprite->width - 1;
      rect.MaxY = y + height - 1;

      /* Initialize the hook */
      hook.hook.h_Entry = (HOOKFUNC)HookFunc_BltMask;
      hook.srcBitMap = bmap;
      hook.srcx = sprite->offx;
      hook.srcy = sprite->offy;
      hook.destx = x;
      hook.desty = y;

      /* Initialize a bitmap where all plane pointers points to the mask */
      InitBitMap(&hook.maskBitMap,src_depth,GetBitMapAttr(bmap,BMA_WIDTH),GetBitMapAttr(bmap,BMA_HEIGHT));
      while (src_depth)
	hook.maskBitMap.Planes[--src_depth] = mask;

      /* Blit onto the Rastport */
      DoHookClipRects(&hook.hook,rp,&rect);
    } else
    {
      BltMaskBitMapRastPort(bmap, sprite->offx, sprite->offy,
			    rp, x, y, sprite->width, height, 0xe2, mask);

    }
  }
}

/****************************************************************
 Draw the whole sprite on position x,y in the Rastport (masked)
*****************************************************************/
void put_sprite_overlay(struct RastPort *rp, struct Sprite *sprite, LONG x, LONG y)
{
  put_sprite_overlay_height(rp, sprite, x, y, sprite->height);
}

/****************************************************************
...
*****************************************************************/
void put_city_output_tile(struct RastPort *rp,
int food, int shield, int trade, int offx, int offy, int x_tile, int y_tile)
{
  int destx = x_tile * get_normal_tile_width() + offx;
  int desty = y_tile * get_normal_tile_height() + offy;

  food = CLIP(0, food, NUM_TILES_DIGITS - 1);
  trade = CLIP(0, trade, NUM_TILES_DIGITS - 1);
  shield = CLIP(0, shield, NUM_TILES_DIGITS - 1);

  put_sprite_overlay(rp, sprites.city.tile_foodnum[food], destx, desty);
  put_sprite_overlay(rp, sprites.city.tile_shieldnum[shield], destx, desty);
  put_sprite_overlay(rp, sprites.city.tile_tradenum[trade], destx, desty);
}

/****************************************************************
 Draw a unit into the rastport at location x1,y1
*****************************************************************/
void put_unit_tile(struct RastPort *rp, struct unit *punit, int x1, int y1)
{

  struct Sprite *sprites[40];
  int count = fill_unit_sprite_array(sprites, punit);

  if (count)
  {
    int i;

    if (sprites[0])
    {
      if (flags_are_transparent)
      {
	put_sprite_overlay(rp, sprites[0], x1, y1);
      }
      else
      {
	put_sprite(rp, sprites[0], x1, y1);
      }
    }
    else
    {
      SetAPen(rp, 1);
      RectFill(rp, x1, y1, x1 + NORMAL_TILE_WIDTH - 1, y1 + NORMAL_TILE_HEIGHT - 1);
    }

    for (i = 1; i < count; i++)
    {
      if (sprites[i])
	put_sprite_overlay(rp, sprites[i], x1, y1);
    }
  }
}

/**************************************************************************
 put a tile onto the screen
 x,y = coordinates of tile on the visible area
 abs_x0, abs_y0 = real coordinates of the tile on the mao
 citymode = Drawed for the CityMap
**************************************************************************/
void put_tile(struct RastPort *rp, int destx, int desty, int x, int y, int abs_x0, int abs_y0, int citymode)
{
  struct Sprite *sprites[80];
  int count = fill_tile_sprite_array(sprites, abs_x0, abs_y0, citymode);

  int x1 = destx + x * NORMAL_TILE_WIDTH;
  int y1 = desty + y * NORMAL_TILE_HEIGHT;
  int x2 = x1 + NORMAL_TILE_WIDTH - 1;
  int y2 = y1 + NORMAL_TILE_HEIGHT - 1;

  if (count)
  {
    int i;

    if (sprites[0])
    {
      /* first tile without mask */
      put_sprite(rp, sprites[0], x1, y1);
    }
    else
    {
      /* normally when solid_color_behind_units */
      struct city *pcity;
      struct player *pplayer = NULL;

      if (count > 1 && !sprites[1])
      {
	/* it's the unit */
	struct tile *ptile;
	struct unit *punit;
	ptile = map_get_tile(abs_x0, abs_y0);
	if ((punit = find_visible_unit(ptile)))
	  pplayer = &game.players[punit->owner];

      }
      else
      {
	/* it's the city */
	if ((pcity = map_get_city(abs_x0, abs_y0)))
	  pplayer = &game.players[pcity->owner];
      }

      if (pplayer)
      {
	SetAPen(rp, 1);
	RectFill(rp, x1, y1, x2, y2);
      }
    }

    for (i = 1; i < count; i++)
    {
      /* to be on the safe side (should not happen) */
      if (sprites[i])
	put_sprite_overlay(rp, sprites[i], x1, y1);
    }


    if (draw_map_grid && !citymode)
    {
      int here_in_radius = player_in_city_radius(game.player_ptr, abs_x0, abs_y0);

      /* left side... */

      if ((map_get_tile(abs_x0 - 1, abs_y0))->known && (here_in_radius || player_in_city_radius(game.player_ptr, abs_x0 - 1, abs_y0)))
      {
	SetAPen(rp, 2);		/* white */
      }
      else
      {
	SetAPen(rp, 1);		/* white */
      }

      Move(rp, x1, y1);
      Draw(rp, x1, y2 + 1);

      /* top side... */
      if ((map_get_tile(abs_x0, abs_y0 - 1))->known && (here_in_radius || player_in_city_radius(game.player_ptr, abs_x0, abs_y0 - 1)))
      {
	SetAPen(rp, 2);		/* white */
      }
      else
      {
	SetAPen(rp, 1);		/* black */
      }

      Move(rp, x1, y1);
      Draw(rp, x2 + 1, y1);
    }
  }
  else
  {
    /* tile is unknow */
    SetAPen(rp, 1);
    RectFill(rp, x1, y1, x2, y2);
  }
}

