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
#include "log.h"
#include "map.h"
#include "options.h"

#include "control.h"
#include "goto.h"
#include "graphics.h"
#include "mapview.h"
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
	  printf(_("Could not get the mask although there must be one (%s)! Graphics may look corrupt.\n"),filename);
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

VOID MyBltMaskBitMapRastPort(struct BitMap *srcBitMap, LONG xSrc, LONG ySrc, struct RastPort *destRP, LONG xDest, LONG yDest, LONG xSize, LONG ySize, ULONG minterm, APTR bltMask)
{
  if (GetBitMapAttr(srcBitMap,BMA_FLAGS)&BMF_INTERLEAVED)
  {
    LONG src_depth = GetBitMapAttr(srcBitMap,BMA_DEPTH);
    struct Rectangle rect;
    struct BltMaskHook hook;
		
    /* Define the destination rectangle in the rastport */
    rect.MinX = xDest;
    rect.MinY = yDest;
    rect.MaxX = xDest + xSize - 1;
    rect.MaxY = yDest + ySize - 1;
		
    /* Initialize the hook */
    hook.hook.h_Entry = (HOOKFUNC)HookFunc_BltMask;
/*    hook.hook.h_Data = (void*)getreg(REG_A4);*/
    hook.srcBitMap = srcBitMap;
    hook.srcx = xSrc;
    hook.srcy = ySrc;
    hook.destx = xDest;
    hook.desty = yDest;
		
    /* Initialize a bitmap where all plane pointers points to the mask */
    InitBitMap(&hook.maskBitMap,src_depth,GetBitMapAttr(srcBitMap,BMA_WIDTH),GetBitMapAttr(srcBitMap,BMA_HEIGHT));
    while (src_depth)
      hook.maskBitMap.Planes[--src_depth] = bltMask;

    /* Blit onto the Rastport */
    DoHookClipRects(&hook.hook,destRP,&rect);
  } else
  {
    BltMaskBitMapRastPort(srcBitMap, xSrc, ySrc, destRP, xDest, yDest, xSize, ySize, minterm, bltMask);
  }
}

/****************************************************************
 Draw the sprite on position x,y in the Rastport (masked)
 restricted to height and width with offset
*****************************************************************/
void put_sprite_overlay_total(struct RastPort *rp, struct Sprite *sprite, LONG x, LONG y, LONG ox, LONG oy, LONG width, LONG height)
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
    MyBltMaskBitMapRastPort(bmap, sprite->offx + ox, sprite->offy + oy,
			    rp, x, y, MIN(width,sprite->width), MIN(height,sprite->height), 0xe2, mask);
  }
}

/****************************************************************
 Draw the sprite on position x,y in the Rastport (masked)
 restricted to height
*****************************************************************/
void put_sprite_overlay_height(struct RastPort *rp, struct Sprite *sprite, LONG x, LONG y, LONG height)
{
  put_sprite_overlay_total(rp,sprite,x,y,0,0,sprite->width, height);
}

/****************************************************************
 Draw the whole sprite on position x,y in the Rastport (masked)
*****************************************************************/
void put_sprite_overlay(struct RastPort *rp, struct Sprite *sprite, LONG x, LONG y)
{
  put_sprite_overlay_height(rp, sprite, x, y, sprite->height);
}

/****************************************************************
 Put a black tile
*****************************************************************/
void put_black_tile(struct RastPort *rp, int canvas_x, int canvas_y)
{
  SetAPen(rp, 1);
  RectFill(rp, canvas_x, canvas_y, canvas_x + NORMAL_TILE_WIDTH - 1, canvas_y + NORMAL_TILE_HEIGHT - 1);
}


/****************************************************************
...
*****************************************************************/
void put_city_output_tile(struct RastPort *rp, int food, int shield, int trade, int offx, int offy, int x_tile, int y_tile)
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

/**************************************************************************
Not used in isometric view.
**************************************************************************/
void put_line(struct RastPort *rp, int dest_x, int dest_y, int x, int y, int dir)
{
  int canvas_src_x, canvas_src_y, canvas_dest_x, canvas_dest_y;
  get_canvas_xy(x, y, &canvas_src_x, &canvas_src_y);
  canvas_src_x += NORMAL_TILE_WIDTH/2;
  canvas_src_y += NORMAL_TILE_HEIGHT/2;
  canvas_dest_x = canvas_src_x + (NORMAL_TILE_WIDTH * DIR_DX[dir])/2;
  canvas_dest_y = canvas_src_y + (NORMAL_TILE_WIDTH * DIR_DY[dir])/2;

  SetAPen(rp,2); /* was CYAN */
  Move(rp,canvas_src_x+dest_x, canvas_src_y+dest_y);
  Draw(rp,canvas_dest_x+dest_x, canvas_dest_y+dest_y);
}

/****************************************************************
 Draw a unit into the rastport at location x1,y1
*****************************************************************/
void put_unit_tile(struct RastPort *rp, struct unit *punit, int x1, int y1)
{
  struct Sprite *sprites[40];
  int solid_bg;
  int count = fill_unit_sprite_array(sprites, punit, &solid_bg);

  if (count)
  {
    int i;

    if (solid_bg)
    {
      SetAPen(rp, 1);
      RectFill(rp, x1, y1, x1 + NORMAL_TILE_WIDTH - 1, y1 + NORMAL_TILE_HEIGHT - 1);
    }
    else
    {
      if (flags_are_transparent) put_sprite_overlay(rp, sprites[0], x1, y1);
      else put_sprite(rp, sprites[0], x1, y1);
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
 abs_x0, abs_y0 = real coordinates of the tile on the map
 citymode = Drawed for the CityMap
**************************************************************************/
void put_tile(struct RastPort *rp, int x, int y, int canvas_x, int canvas_y, int citymode)
{
  struct Sprite *tile_sprs[80];
  int fill_bg;
  struct player *pplayer;

  if (normalize_map_pos(&x, &y) && tile_is_known(x, y)) {
    int count = fill_tile_sprite_array(tile_sprs, x, y, citymode, &fill_bg, &pplayer);
    int i = 0;

    if (fill_bg) {
      if (pplayer) {
      	SetAPen(rp,2); /* should be a player color */
      } else {
        SetAPen(rp,1); /* COLOR_STD_BACKGROUND */
      }
      RectFill(rp,canvas_x,canvas_y,canvas_x + NORMAL_TILE_WIDTH - 1, canvas_y + NORMAL_TILE_HEIGHT - 1);
    } else {
      /* first tile without mask */
      put_sprite(rp, tile_sprs[0], canvas_x, canvas_y);
      i++;
    }

    for (;i<count; i++) {
      if (tile_sprs[i]) {
	put_sprite_overlay(rp, tile_sprs[i],canvas_x, canvas_y);
      }
    }

    if (draw_map_grid && !citymode) {
      int here_in_radius =
	player_in_city_radius(game.player_ptr, x, y);
      /* left side... */
      if ((map_get_tile(x-1, y))->known &&
	  (here_in_radius ||
	   player_in_city_radius(game.player_ptr, x-1, y))) {
	SetAPen(rp,2); /* white */
      } else {
        SetAPen(rp,1); /* black */
      }
      Move(rp,canvas_x,canvas_y);
      Draw(rp,canvas_x,canvas_y + NORMAL_TILE_HEIGHT);

      /* top side... */
      if((map_get_tile(x, y-1))->known &&
	 (here_in_radius ||
	  player_in_city_radius(game.player_ptr, x, y-1))) {
	SetAPen(rp,2); /* white */
      } else {
	SetAPen(rp,1); /* black */
      }
      Move(rp,canvas_x,canvas_y);
      Draw(rp,canvas_x + NORMAL_TILE_WIDTH,canvas_y);
    }

    if (draw_coastline && !draw_terrain) {
      enum tile_terrain_type t1 = map_get_terrain(x, y), t2;
      int x1 = x-1, y1 = y;
      SetAPen(rp,3); /* blue!! */
      if (normalize_map_pos(&x1, &y1)) {
	t2 = map_get_terrain(x1, y1);
	/* left side */
	if ((t1 == T_OCEAN) ^ (t2 == T_OCEAN))
	{
	  Move(rp, canvas_x, canvas_y);
	  Draw(rp, canvas_x, canvas_y + NORMAL_TILE_HEIGHT);
	}
      }
      /* top side */
      x1 = x; y1 = y-1;
      if (normalize_map_pos(&x1, &y1)) {
	t2 = map_get_terrain(x1, y1);
	if ((t1 == T_OCEAN) ^ (t2 == T_OCEAN))
	{
	  Move(rp, canvas_x, canvas_y);
	  Draw(rp, canvas_x + NORMAL_TILE_WIDTH, canvas_y);
	}
      }
    }
  } else {
    /* tile is unknown */
    put_black_tile(rp, canvas_x, canvas_y);
  }

  if (!citymode) {
    /* put any goto lines on the tile. */
    if (y >= 0 && y < map.ysize) {
      int dir;
      for (dir = 0; dir < 8; dir++) {
	if (get_drawn(x, y, dir)) {
	  put_line(rp, 0,0,x, y, dir);
	}
      }
    }

    /* Some goto lines overlap onto the tile... */
    if (NORMAL_TILE_WIDTH%2 == 0 || NORMAL_TILE_HEIGHT%2 == 0) {
      int line_x = x - 1;
      int line_y = y;
      if (normalize_map_pos(&line_x, &line_y)
	  && get_drawn(line_x, line_y, 2)) {
	/* it is really only one pixel in the top right corner */
	put_line(rp, 0,0,line_x, line_y, 2);
      }
    }
  }
}

/***************************************************************************
...
***************************************************************************/
int isometric_view_supported(void)
{
  return 1;
}

/***************************************************************************
...
***************************************************************************/
int overhead_view_supported(void)
{
  return 1;
}

/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void put_black_tile_iso(struct RastPort *rp,
			       int canvas_x, int canvas_y,
			       int offset_x, int offset_y,
			       int width, int height)
{
  assert(width <= NORMAL_TILE_WIDTH);
  assert(height <= NORMAL_TILE_HEIGHT);
  put_sprite_overlay_total(rp, sprites.black_tile, canvas_x + offset_x, canvas_y + offset_y, offset_x, offset_y, width, height);
}

/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void put_overlay_tile_draw(struct RastPort *rp,
				  int canvas_x, int canvas_y,
				  struct Sprite *ssprite,
				  int offset_x, int offset_y,
				  int width, int height,
				  int fog)
{
  if (!ssprite || !width || !height)
    return;


  put_sprite_overlay_total(rp, ssprite, canvas_x + offset_x, canvas_y + offset_y, offset_x, offset_y, width, height);

  /* I imagine this could be done more efficiently. Some pixels We first
     draw from the sprite, and then draw black afterwards. It would be much
     faster to just draw every second pixel black in the first place. */

  if (fog) {
/*
    gdk_gc_set_clip_origin(fill_tile_gc, canvas_x, canvas_y);
    gdk_gc_set_clip_mask(fill_tile_gc, ssprite->mask);
    gdk_gc_set_foreground(fill_tile_gc, colors_standard[COLOR_STD_BLACK]);
    gdk_gc_set_stipple(fill_tile_gc, black50);

    gdk_draw_rectangle(pixmap, fill_tile_gc, TRUE,
		       canvas_x+offset_x, canvas_y+offset_y,
		       MIN(width, MAX(0, ssprite->width-offset_x)),
		       MIN(height, MAX(0, ssprite->height-offset_y)));
    gdk_gc_set_clip_mask(fill_tile_gc, NULL);
*/
  }
}

/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void put_city_draw(struct city *pcity, struct RastPort *rp,
			  int canvas_x, int canvas_y,
			  int offset_x, int offset_y_unit,
			  int width, int height_unit,
			  int fog)
{
  struct Sprite *sprites[80];
  int count = fill_city_sprite_array_iso(sprites, pcity);
  int i;

  for (i=0; i<count; i++) {
    if (sprites[i]) {
      put_overlay_tile_draw(rp, canvas_x, canvas_y, sprites[i],
			    offset_x, offset_y_unit,
			    width, height_unit,
			    fog);
    }
  }
}

/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void put_unit_draw(struct unit *punit, struct RastPort *rp,
			  int canvas_x, int canvas_y,
			  int offset_x, int offset_y_unit,
			  int width, int height_unit)
{
  struct Sprite *sprites[40];
  int dummy;
  int count = fill_unit_sprite_array(sprites, punit, &dummy);
  int i;

  for (i=0; i<count; i++) {
    if (sprites[i]) {
      put_overlay_tile_draw(rp, canvas_x, canvas_y, sprites[i],
			    offset_x, offset_y_unit,
			    width, height_unit, 0);
    }
  }
}


/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void put_tile_iso(struct RastPort *rp, int x, int y,
				int canvas_x, int canvas_y,
				int citymode,
				int offset_x, int offset_y, int offset_y_unit,
				int width, int height, int height_unit,
				enum draw_type draw)
{
  struct Sprite *tile_sprs[80];
  struct Sprite *coasts[4];
  struct Sprite *dither[4];
  struct city *pcity;
  struct unit *punit, *pfocus;
  enum tile_special_type special;
  int count, i = 0;
  int fog;
  int solid_bg;

  if (!width || !(height || height_unit))
    return;

  count = fill_tile_sprite_array_iso(tile_sprs, coasts, dither,
				     x, y, citymode, &solid_bg);

  if (count == -1) { /* tile is unknown */
    put_black_tile_iso(rp, canvas_x, canvas_y,
			   offset_x, offset_y, width, height);
    return;
  }

  assert(normalize_map_pos(&x, &y));
  fog = tile_is_known(x, y) == TILE_KNOWN_FOGGED && draw_fog_of_war;
  pcity = map_get_city(x, y);
  punit = get_drawable_unit(x, y, citymode);
  pfocus = get_unit_in_focus();
  special = map_get_special(x, y);

/*  if (solid_bg) {
    gdk_gc_set_clip_origin(fill_bg_gc, canvas_x, canvas_y);
    gdk_gc_set_clip_mask(fill_bg_gc, sprites.black_tile->mask);
    gdk_gc_set_foreground(fill_bg_gc, colors_standard[COLOR_STD_BACKGROUND]);

    gdk_draw_rectangle(pm, fill_bg_gc, TRUE,
		       canvas_x+offset_x, canvas_y+offset_y,
		       MIN(width, MAX(0, sprites.black_tile->width-offset_x)),
		       MIN(height, MAX(0, sprites.black_tile->height-offset_y)));
    gdk_gc_set_clip_mask(fill_bg_gc, NULL);
    if (fog) {
      gdk_gc_set_clip_origin(fill_tile_gc, canvas_x, canvas_y);
      gdk_gc_set_clip_mask(fill_tile_gc, sprites.black_tile->mask);
      gdk_gc_set_foreground(fill_tile_gc, colors_standard[COLOR_STD_BLACK]);
      gdk_gc_set_stipple(fill_tile_gc, black50);

      gdk_draw_rectangle(pm, fill_tile_gc, TRUE,
			 canvas_x+offset_x, canvas_y+offset_y,
			 MIN(width, MAX(0, sprites.black_tile->width-offset_x)),
			 MIN(height, MAX(0, sprites.black_tile->height-offset_y)));
      gdk_gc_set_clip_mask(fill_tile_gc, NULL);
    }
  }*/

  if (draw_terrain) {
    if (map_get_terrain(x, y) == T_OCEAN) { /* coasts */
      int dx, dy;

/* temporary */
      put_black_tile_iso(rp, canvas_x, canvas_y,
			 offset_x, offset_y, width, height);

      /* top */
      dx = offset_x-NORMAL_TILE_WIDTH/4;
      put_overlay_tile_draw(rp, canvas_x + NORMAL_TILE_WIDTH/4,
			    canvas_y, coasts[0],
			    MAX(0, dx),
			    offset_y,
			    MAX(0, width-MAX(0, -dx)),
			    height,
			    fog);
      /* bottom */
      dx = offset_x-NORMAL_TILE_WIDTH/4;
      dy = offset_y-NORMAL_TILE_HEIGHT/2;
      put_overlay_tile_draw(rp, canvas_x + NORMAL_TILE_WIDTH/4,
				canvas_y + NORMAL_TILE_HEIGHT/2, coasts[1],
				MAX(0, dx),
				MAX(0, dy),
				MAX(0, width-MAX(0, -dx)),
				MAX(0, height-MAX(0, -dy)),
				fog);
      /* left */
      dy = offset_y-NORMAL_TILE_HEIGHT/4;
      put_overlay_tile_draw(rp, canvas_x,
			    canvas_y + NORMAL_TILE_HEIGHT/4, coasts[2],
			    offset_x,
			    MAX(0, dy),
			    width,
			    MAX(0, height-MAX(0, -dy)),
			    fog);
      /* right */
      dx = offset_x-NORMAL_TILE_WIDTH/2;
      dy = offset_y-NORMAL_TILE_HEIGHT/4;
      put_overlay_tile_draw(rp, canvas_x + NORMAL_TILE_WIDTH/2,
			   canvas_y + NORMAL_TILE_HEIGHT/4, coasts[3],
			   MAX(0, dx),
			   MAX(0, dy),
			   MAX(0, width-MAX(0, -dx)),
			   MAX(0, height-MAX(0, -dy)),
			   fog);
    } else {
      put_overlay_tile_draw(rp, canvas_x, canvas_y, tile_sprs[0],
			   offset_x, offset_y, width, height, fog);
      i++;
    }

    /*** Dither base terrain ***/
/*    if (draw_terrain)
      dither_tile(pm, dither, canvas_x, canvas_y,
		  offset_x, offset_y, width, height, fog);*/
  }

  /*** Rest of terrain and specials ***/
  for (; i<count; i++) {
    if (tile_sprs[i])
      put_overlay_tile_draw(rp, canvas_x, canvas_y, tile_sprs[i],
			    offset_x, offset_y, width, height, fog);
    else
      freelog(LOG_ERROR, "sprite is NULL");
  }

  /*** Map grid ***/
/*  if (draw_map_grid) {
    /* we draw the 2 lines on top of the tile; the buttom lines will be
       drawn by the tiles underneath. */
    gdk_gc_set_foreground(thin_line_gc, colors_standard[COLOR_STD_BLACK]);
    if (draw & D_M_R)
      gdk_draw_line(pm, thin_line_gc,
		    canvas_x+NORMAL_TILE_WIDTH/2, canvas_y,
		    canvas_x+NORMAL_TILE_WIDTH, canvas_y+NORMAL_TILE_HEIGHT/2);
    if (draw & D_M_L)
      gdk_draw_line(pm, thin_line_gc,
		    canvas_x, canvas_y + NORMAL_TILE_HEIGHT/2,
		    canvas_x+NORMAL_TILE_WIDTH/2, canvas_y);
  }*/

/*  if (draw_coastline && !draw_terrain) {
    enum tile_terrain_type t1 = map_get_terrain(x, y), t2;
    int x1, y1;
    gdk_gc_set_foreground(thin_line_gc, colors_standard[COLOR_STD_OCEAN]);
    x1 = x; y1 = y-1;
    if (normalize_map_pos(&x1, &y1)) {
      t2 = map_get_terrain(x1, y1);
      if (draw & D_M_R && ((t1 == T_OCEAN) ^ (t2 == T_OCEAN)))
	gdk_draw_line(pm, thin_line_gc,
		      canvas_x+NORMAL_TILE_WIDTH/2, canvas_y,
		      canvas_x+NORMAL_TILE_WIDTH, canvas_y+NORMAL_TILE_HEIGHT/2);
    }
    x1 = x-1; y1 = y;
    if (normalize_map_pos(&x1, &y1)) {
      t2 = map_get_terrain(x1, y1);
      if (draw & D_M_L && ((t1 == T_OCEAN) ^ (t2 == T_OCEAN)))
	gdk_draw_line(pm, thin_line_gc,
		      canvas_x, canvas_y + NORMAL_TILE_HEIGHT/2,
		      canvas_x+NORMAL_TILE_WIDTH/2, canvas_y);
    }
  }*/

  /*** City and various terrain improvements ***/
  if (pcity && draw_cities) {
    put_city_draw(pcity, rp,
		  canvas_x, canvas_y - NORMAL_TILE_HEIGHT/2,
		  offset_x, offset_y_unit,
		  width, height_unit, fog);
  }
  if (special & S_AIRBASE && draw_fortress_airbase)
    put_overlay_tile_draw(rp,
			  canvas_x, canvas_y-NORMAL_TILE_HEIGHT/2,
			  sprites.tx.airbase,
			  offset_x, offset_y_unit,
			  width, height_unit, fog);
  if (special & S_FALLOUT && draw_pollution)
    put_overlay_tile_draw(rp,
			  canvas_x, canvas_y,
			  sprites.tx.fallout,
			  offset_x, offset_y,
			  width, height, fog);
  if (special & S_POLLUTION && draw_pollution)
    put_overlay_tile_draw(rp,
			  canvas_x, canvas_y,
			  sprites.tx.pollution,
			  offset_x, offset_y,
			  width, height, fog);

  /*** city size ***/
  /* Not fogged as it would be unreadable */
  if (pcity && draw_cities) {
    if (pcity->size>=10)
      put_overlay_tile_draw(rp, canvas_x, canvas_y-NORMAL_TILE_HEIGHT/2,
			    sprites.city.size_tens[pcity->size/10],
			    offset_x, offset_y_unit,
			    width, height_unit, 0);

    put_overlay_tile_draw(rp, canvas_x, canvas_y-NORMAL_TILE_HEIGHT/2,
			  sprites.city.size[pcity->size%10],
			  offset_x, offset_y_unit,
			  width, height_unit, 0);
  }

  /*** Unit ***/
  if (punit && (draw_units || (punit == pfocus && draw_focus_unit))) {
    put_unit_draw(punit, rp,
		  canvas_x, canvas_y - NORMAL_TILE_HEIGHT/2,
		  offset_x, offset_y_unit,
		  width, height_unit);
    if (!pcity && unit_list_size(&map_get_tile(x, y)->units) > 1)
      put_overlay_tile_draw(rp,
			    canvas_x, canvas_y-NORMAL_TILE_HEIGHT/2,
			    sprites.unit.stack,
			    offset_x, offset_y_unit,
			    width, height_unit, fog);
  }

  if (special & S_FORTRESS && draw_fortress_airbase)
    put_overlay_tile_draw(rp,
			  canvas_x, canvas_y-NORMAL_TILE_HEIGHT/2,
			  sprites.tx.fortress,
			  offset_x, offset_y_unit,
			  width, height_unit, fog);
}

/**************************************************************************
Only used for isometric view.
**************************************************************************/
void put_one_tile(struct RastPort *rp, int x, int y, enum draw_type draw)
{
  int canvas_x, canvas_y;
  int height, width, height_unit;
  int offset_x, offset_y, offset_y_unit;
  int dest_x = 0, dest_y = 0;

  if (!tile_visible_mapcanvas(x, y)) {
    freelog(LOG_DEBUG, "dropping %d,%d", x, y);
    return;
  }
  freelog(LOG_DEBUG, "putting %d,%d draw %x", x, y, draw);

  width = (draw & D_TMB_L) && (draw & D_TMB_R) ? NORMAL_TILE_WIDTH : NORMAL_TILE_WIDTH/2;
  if (!(draw & D_TMB_L))
    offset_x = NORMAL_TILE_WIDTH/2;
  else
    offset_x = 0;

  height = 0;
  if (draw & D_M_LR) height += NORMAL_TILE_HEIGHT/2;
  if (draw & D_B_LR) height += NORMAL_TILE_HEIGHT/2;
  if (draw & D_T_LR)
    height_unit = height + NORMAL_TILE_HEIGHT/2;
  else
    height_unit = height;

  offset_y = (draw & D_M_LR) ? 0 : NORMAL_TILE_HEIGHT/2;
  if (!(draw & D_T_LR))
    offset_y_unit = (draw & D_M_LR) ? NORMAL_TILE_HEIGHT/2 : NORMAL_TILE_HEIGHT;
  else
    offset_y_unit = 0;

  /* returns whether the tile is visible. */
  if (get_canvas_xy(x, y, &canvas_x, &canvas_y)) {
    if (normalize_map_pos(&x, &y)) {
      put_tile_iso(rp,x,y,dest_x+canvas_x,dest_y+canvas_y,0,offset_x,offset_y,offset_y_unit,
                   width,height,height_unit,draw);
    } else {
      put_black_tile_iso(rp, dest_x+canvas_x, dest_y+canvas_y, offset_x, offset_y,
			     width, height);
    }
  }
}

/**************************************************************************
Only used for isometric view.
**************************************************************************/
void put_one_tile_full(struct RastPort *rp, int x, int y, int canvas_x, int canvas_y, int citymode)
{
  put_tile_iso(rp, x, y, canvas_x, canvas_y, citymode,
	       0, 0, 0,
	       NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT, UNIT_TILE_HEIGHT,
	       D_FULL);
}

