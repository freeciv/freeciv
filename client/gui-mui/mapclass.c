#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <exec/memory.h>
#include <graphics/rpattr.h>
#include <libraries/mui.h>
#include <guigfx/guigfx.h>

#include <clib/alib_protos.h>

#define USE_SYSBASE
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <proto/guigfx.h>
#include <proto/datatypes.h>

#include "fcintl.h"
#include "log.h"
#include "map.h"
#include "game.h"
#include "spaceship.h"

#include "civclient.h"
#include "climisc.h"
#include "clinet.h"
#include "citydlg.h"
#include "control.h"
#include "mapview.h"
#include "graphics.h"
#include "gui_main.h"
#include "options.h"
#include "tilespec.h"

#include "overviewclass.h"
#include "mapclass.h"
#include "muistuff.h"

/* TODO: Split this file! */

APTR pen_shared_map;

extern int flags_are_transparent;
extern int draw_diagonal_roads;
extern int use_solid_color_behind_units;

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
 Request a unit to go to a location
 (GUI Independend)
*****************************************************************/
void request_unit_goto_location(struct unit *punit, int x, int y)
{
  struct packet_unit_request req;
  req.unit_id = punit->id;
  req.name[0] = '\0';
  req.x = x;
  req.y = y;
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_GOTO_TILE);
}

/* Own sprite list - used so we can reload the sprites if needed */

static struct MinList sprite_list;
static LONG sprite_initialized;

struct SpriteNode /* could better embed this in struct Sprite */
{
  struct MinNode node;
  struct Sprite *sprite;
  STRPTR filename;
};


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
 Allocate and load a sprite
*****************************************************************/
static struct Sprite *load_sprite(const char *filename, ULONG usemask)
{
  struct Sprite *sprite = (struct Sprite *) malloc(sizeof(struct Sprite));	//AllocVec(sizeof(struct Sprite),MEMF_CLEAR);

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
	  printf("Could not get the mask altrough there must be one!Graphics may look corrupt\n");
	}

	return sprite;
      }
    }
    free(sprite);
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
      node->filename = StrCopy(filename);
      AddTail((struct List *) &sprite_list, (struct Node *) node);
      return sprite;
    }
    FreeVec(node);
  }

  return NULL;
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
  struct Sprite *sprite = (struct Sprite *) malloc(sizeof(struct Sprite));	//AllocVec(sizeof(struct Sprite),MEMF_CLEAR);

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
...
*****************************************************************/
void free_intro_radar_sprites(void)
{
}

/****************************************************************
 Cleanup the sprites
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
static void cleanup_sprites(void)
{
  struct SpriteNode *node = (struct SpriteNode *) sprite_list.mlh_Head;
  while (node->node.mln_Succ)
  {
    cleanup_sprite(node->sprite);
    node = (struct SpriteNode *) node->node.mln_Succ;
  }
}

/****************************************************************
 Dummy (used by the gui independent part)
*****************************************************************/
void free_sprite(struct Sprite *sprite)
{
  /* Just a dummy */
}

/****************************************************************
 Free the sprite
*****************************************************************/
static void real_free_sprite(struct Sprite *sprite)
{
  cleanup_sprite(sprite);
  free(sprite);
}

/****************************************************************
 Free all sprites
*****************************************************************/
static void free_sprites(void)
{
  struct SpriteNode *node = (struct SpriteNode *) sprite_list.mlh_Head;
  while ((node = (struct SpriteNode *) RemHead((struct List *) &sprite_list)))
  {
    real_free_sprite(node->sprite);
    if (node->filename)
      FreeVec(node->filename);
    FreeVec(node);
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
      if (flags & BMF_INTERLEAVED)
	array_width *= GetBitMapAttr(sprite->bitmap, BMA_DEPTH);

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
static int render_sprites(APTR drawhandle)
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
  frame = CLIP(0, frame, NUM_TILES_CITIZEN - 1);
  return sprites.citizen[frame];
}

/****************************************************************
 Returns a thumb sprite
*****************************************************************/
struct Sprite *get_thumb_sprite(int onoff)
{
  return sprites.treaty_thumb[!!onoff];
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
static void put_sprite(struct RastPort *rp, struct Sprite *sprite, LONG x, LONG y)
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

/****************************************************************
 Draw the sprite on position x,y in the Rastport (masked)
 restricted to height
*****************************************************************/
static void put_sprite_overlay_height(struct RastPort *rp, struct Sprite *sprite, LONG x, LONG y, LONG height)
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
    BltMaskBitMapRastPort(bmap, sprite->offx, sprite->offy,
			  rp, x, y, sprite->width, height, 0xe2, mask);
  }
}

/****************************************************************
 Draw the whole sprite on position x,y in the Rastport (masked)
*****************************************************************/
static void put_sprite_overlay(struct RastPort *rp, struct Sprite *sprite, LONG x, LONG y)
{
  put_sprite_overlay_height(rp, sprite, x, y, sprite->height);
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
      /* normally when use_solid_color_behind_units */
      struct city *pcity;
      struct player *pplayer = NULL;

      if (count > 1 && !sprites[1])
      {
	/* it's the unit */
	struct tile *ptile;
	struct unit *punit;
	ptile = map_get_tile(abs_x0, abs_y0);
	if ((punit = player_find_visible_unit(game.player_ptr, ptile)))
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


/****************************************************************
 TilePopWindow Custom Class
*****************************************************************/

static struct MUI_CustomClass *CL_TilePopWindow;	/* only here used */


STATIC ULONG TilePopWindow_New(struct IClass *cl, Object * o, struct opSet *msg)
{
  Object *obj = NULL;

  int xtile = GetTagData(MUIA_TilePopWindow_X, 0, msg->ops_AttrList);
  int ytile = GetTagData(MUIA_TilePopWindow_Y, 0, msg->ops_AttrList);

  /* from mapctrl.c/popit */
  static struct map_position cross_list[2 + 1];
  struct map_position *cross_head = cross_list;
  int i, count = 0;
  int popx, popy;
  char s[512];
  struct city *pcity;
  struct unit *punit;
  struct tile *ptile = map_get_tile(xtile, ytile);

  if (ptile->known >= TILE_KNOWN)
  {
    Object *group;

    obj = (Object *) DoSuperNew(cl, o,
				MUIA_Window_Activate, FALSE,
				MUIA_Window_Borderless, TRUE,
				MUIA_Window_CloseGadget, FALSE,
				MUIA_Window_DepthGadget, FALSE,
				MUIA_Window_DragBar, FALSE,
				MUIA_Window_SizeGadget, FALSE,
				MUIA_Window_LeftEdge, MUIV_Window_LeftEdge_Moused,
				MUIA_Window_TopEdge, MUIV_Window_TopEdge_Moused,
				WindowContents, group = VGroup,
				TextFrame,
				End,
				TAG_MORE, msg->ops_AttrList);

    if (obj)
    {
      Object *text_obj;
      sprintf(s, _("Terrain: %s"), map_get_tile_info_text(xtile, ytile));
      text_obj = TextObject, MUIA_Text_Contents, s, End;
      DoMethod(group, OM_ADDMEMBER, text_obj);

      if (ptile->special & S_HUT)
      {
	text_obj = TextObject, MUIA_Text_Contents, _("Minor Tribe Village"), End;
	DoMethod(group, OM_ADDMEMBER, text_obj);
      }

      if ((pcity = map_get_city(xtile, ytile)))
      {
	sprintf(s, "City: %s(%s)", pcity->name, get_nation_name(game.players[pcity->owner].nation));
	text_obj = TextObject, MUIA_Text_Contents, s, End;

	if(text_obj)
	  DoMethod(group, OM_ADDMEMBER, text_obj);

	if (city_got_citywalls(pcity)) {
	  text_obj = TextObject, MUIA_Text_Contents, "with City Walls", End;
	  if (text_obj)
	    DoMethod(group, OM_ADDMEMBER, text_obj);
	}
      }

      if (get_tile_infrastructure_set(ptile))
      {
	strcpy(s, _("Infrastructure: "));
	strcat(s, map_get_infrastructure_text(ptile->special));
	text_obj = TextObject, MUIA_Text_Contents, s, End;
	DoMethod(group, OM_ADDMEMBER, text_obj);
      }

      if ((punit = player_find_visible_unit(game.player_ptr, ptile)) && !pcity)
      {
	char cn[64];
	struct unit_type *ptype = get_unit_type(punit->type);
	cn[0] = '\0';
	if (punit->owner == game.player_idx)
	{
	  struct city *pcity;
	  pcity = city_list_find_id(&game.player_ptr->cities, punit->homecity);
	  if (pcity)
	    sprintf(cn, "/%s", pcity->name);
	}
	sprintf(s, _("Unit: %s(%s%s)"), ptype->name, get_nation_name(game.players[punit->owner].nation), cn);

	text_obj = TextObject, MUIA_Text_Contents, s, End;
	DoMethod(group, OM_ADDMEMBER, text_obj);

	if (punit->owner == game.player_idx)
	{
	  char uc[64] = "";
	  if (unit_list_size(&ptile->units) >= 2)
	    sprintf(uc, _("  (%d more)"), unit_list_size(&ptile->units) - 1);

	  sprintf(s, "A:%d D:%d FP:%d HP:%d/%d%s%s", ptype->attack_strength,
		  ptype->defense_strength, ptype->firepower, punit->hp,
		  ptype->hp, punit->veteran ? " V" : "", uc);

	  if (punit->activity == ACTIVITY_GOTO)
	  {
	    cross_head->x = punit->goto_dest_x;
	    cross_head->y = punit->goto_dest_y;
	    cross_head++;
	  }
	}
	else
	{
	  sprintf(s, "A:%d D:%d FP:%d HP:%d0%%", ptype->attack_strength,
		  ptype->defense_strength, ptype->firepower,
		  (punit->hp * 100 / ptype->hp + 9) / 10);
	}

	text_obj = TextObject, MUIA_Text_Contents, s, End;
	DoMethod(group, OM_ADDMEMBER, text_obj);
      }

      cross_head->x = xtile;
      cross_head->y = ytile;
      cross_head++;
      cross_head->x = -1;
/*
   for (i = 0; cross_list[i].x >= 0; i++)
   {
   put_cross_overlay_tile(cross_list[i].x,cross_list[i].y);
   }
 */
    }
  }

  return ((ULONG) obj);
}

STATIC __saveds __asm ULONG TilePopWindow_Dispatcher(register __a0 struct IClass * cl, register __a2 Object * obj, register __a1 Msg msg)
{
  switch (msg->MethodID)
  {
  case OM_NEW:
    return (TilePopWindow_New(cl, obj, (struct opSet *) msg));
  }

  return (DoSuperMethodA(cl, obj, msg));
}

/****************************************************************
 Map Custom Class
*****************************************************************/

struct MUI_CustomClass *CL_Map;

Object *MakeMap(void)
{
  return MapObject,
    InnerSpacing(0,0),
    VirtualFrame,
    MUIA_Font, MUIV_Font_Big,
    End;
}

struct Map_Data
{
  /* Über Tags (2 Sprites) */
  struct Sprite *intro_gfx_sprite;
  struct Sprite *radar_gfx_sprite;

  LONG m_black;

  /* Connected Objects */
  Object *overview_object;
  Object *hscroller_object;
  Object *vscroller_object;

  /* Owns Object */
  Object *pop_wnd;
  Object *context_menu;

  LONG horiz_first;
  LONG vert_first;
  LONG shown;			/* if object is displayed */
  struct Hook newpos_hook;
  struct Map_Click click;	/* Clicked Tile on the Map */

  APTR drawhandle;

  struct BitMap *map_bitmap;
  struct Layer_Info *map_layerinfo;
  struct Layer *map_layer;

  struct BitMap *unit_bitmap;
  struct Layer_Info *unit_layerinfo;
  struct Layer *unit_layer;

  LONG update;			/* 1 = Tile; 2 = MoveUnit; 3 = Scroll; 4 = ShowCityNames */
  LONG tilex, tiley, width, height, write_to_screen;	/* Drawing (1) */
  LONG dest_x, dest_y, x0, y0, dx, dy;	/* Drawing (2) */
  struct unit *punit;		/* Drawing (2) */
  LONG old_horiz_first;		/* Drawing (3) */
  LONG old_vert_first;		/* Drawing (3) */
};

struct NewPosData
{
  struct Map_Data *data;
  ULONG type;
};


__asm __saveds STATIC ULONG Map_NewPosFunc(register __a2 Object * map_object, register __a1 struct NewPosData *npd)
{
  switch (npd->type)
  {
  case 0:
    SetAttrs(map_object,
	     MUIA_Map_HorizFirst, xget(npd->data->overview_object, MUIA_Overview_RectLeft),
	     MUIA_Map_VertFirst, xget(npd->data->overview_object, MUIA_Overview_RectTop),
	     TAG_DONE);
    break;

  case 1:
    set(map_object, MUIA_Map_HorizFirst, xget(npd->data->hscroller_object, MUIA_Prop_First));
    break;

  case 2:
    set(map_object, MUIA_Map_VertFirst, xget(npd->data->vscroller_object, MUIA_Prop_First));
    break;
  }

  return 0;
}


struct Command_Node
{
  struct MinNode node;
  STRPTR name;
  ULONG id;
};

struct Command_List
{
  struct MinList list;
  APTR pool;
};

STATIC VOID Command_List_Sort(struct Command_List *list)
{
  BOOL notfinished = TRUE;

  /* Sort list (quick & dirty bubble sort) */
  while (notfinished)
  {
    struct Command_Node *first;

    /* Reset not finished flag */
    notfinished = FALSE;

    /* Get first node */
    if ((first = (struct Command_Node *) List_First(list)))
    {
      struct Command_Node *second;

      /* One bubble sort round */
      while ((second = (struct Command_Node *) Node_Next(first)))
      {
	if (strcmp(first->name, second->name) > 0)
	{
	  Remove((struct Node *) first);
	  Insert((struct List *) list, (struct Node *) first, (struct Node *) second);
	  notfinished = TRUE;
	}
	else
	  first = second;
      }
    }
  }
}


#define PACK_USERDATA(punit,command) ((punit?((punit->id+1) << 8):0) | command)
#define PACK_CITY_USERDATA(punit,command) ((punit?((punit->id+1) << 8):0) | command)
#define UNPACK_UNIT(packed) ((packed>>8)?(find_unit_by_id((packed >> 8)-1)):NULL)
#define UNPACK_CITY(packed) ((packed>>8)?(find_city_by_id((packed >> 8)-1)):NULL)
#define UNPACK_COMMAND(packed) (packed&0xff)

VOID Map_MakeContextItem(Object * menu_title, STRPTR name, ULONG udata)
{
  Object *entry = MUI_MakeObject(MUIO_Menuitem, name, NULL, MUIO_Menuitem_CopyStrings, 0);
  if (entry)
  {
    set(entry, MUIA_UserData, udata);
    DoMethod(menu_title, MUIM_Family_AddTail, entry);
  }
}

VOID Map_MakeContextBarlabel(Object * menu_title)
{
  Object *entry = MUI_MakeObject(MUIO_Menuitem, ~0, NULL, 0, 0);
  if (entry)
  {
    set(entry, MUIA_UserData, NULL);
    DoMethod(menu_title, MUIM_Family_AddTail, entry);
  }
}

struct Command_Node *Map_InsertCommand(struct Command_List *list, STRPTR name, ULONG id)
{
  struct Command_Node *node = (struct Command_Node *) AllocVec(sizeof(struct Command_Node), 0);
  if (node)
  {
    node->name = name;
    node->id = id;
    AddTail((struct List *) list, (struct Node *) node);
  }
  return node;
}

STATIC ULONG Map_New(struct IClass * cl, Object * o, struct opSet * msg)
{
  if ((o = (Object *) DoSuperMethodA(cl, o, (Msg) msg)))
  {
    struct Map_Data *data = (struct Map_Data *) INST_DATA(cl, o);
    data->newpos_hook.h_Entry = (HOOKFUNC) Map_NewPosFunc;

    set(o, MUIA_ContextMenu, 1);
  }
  return (ULONG) o;
}

STATIC ULONG Map_Dispose(struct IClass * cl, Object * o, Msg msg)
{
  struct Map_Data *data = (struct Map_Data *) INST_DATA(cl, o);
  if (data->context_menu)
    MUI_DisposeObject(data->context_menu);
  return DoSuperMethodA(cl, o, msg);
}

STATIC ULONG Map_Set(struct IClass * cl, Object * o, struct opSet * msg)
{
  struct Map_Data *data = (struct Map_Data *) INST_DATA(cl, o);
  struct TagItem *tl = msg->ops_AttrList;
  struct TagItem *ti;

  BOOL redraw = FALSE;

  LONG new_horiz_first = data->horiz_first;
  LONG new_vert_first = data->vert_first;

  while ((ti = NextTagItem(&tl)))
  {
    switch (ti->ti_Tag)
    {
    case MUIA_Map_HorizFirst:
      new_horiz_first = ti->ti_Data;
      redraw = TRUE;
      break;

    case MUIA_Map_VertFirst:
      new_vert_first = ti->ti_Data;
      redraw = TRUE;
      break;

    case MUIA_Map_Overview:
      if ((data->overview_object = (Object *) ti->ti_Data))
      {
	DoMethod(data->overview_object, MUIM_Notify, MUIA_Overview_NewPos, MUIV_EveryTime, o, 4, MUIM_CallHook, &data->newpos_hook, data, 0);
	if (data->overview_object)
	{
	  SetAttrs(data->overview_object,
		   data->radar_gfx_sprite ? MUIA_Overview_RadarPicture : TAG_IGNORE, data->radar_gfx_sprite ? data->radar_gfx_sprite->picture : NULL,
		   MUIA_Overview_RectWidth, xget(o, MUIA_Map_HorizVisible),
		   MUIA_Overview_RectHeight, xget(o, MUIA_Map_VertVisible),
		   TAG_DONE);
	}
      }
      break;

    case MUIA_Map_HScroller:
      if ((data->hscroller_object = (Object *) ti->ti_Data))
      {
	DoMethod(data->hscroller_object, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, o, 4, MUIM_CallHook, &data->newpos_hook, data, 1);
      }
      break;

    case MUIA_Map_VScroller:
      if ((data->vscroller_object = (Object *) ti->ti_Data))
      {
	DoMethod(data->vscroller_object, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, o, 4, MUIM_CallHook, &data->newpos_hook, data, 2);
      }
      break;
    }
  }

  if (redraw && get_client_state() == CLIENT_GAME_RUNNING_STATE)
  {
    data->old_horiz_first = data->horiz_first;
    data->old_vert_first = data->vert_first;
    data->horiz_first = new_horiz_first;
    data->vert_first = new_vert_first;

    nnset(data->hscroller_object, MUIA_Prop_First, data->horiz_first);
    nnset(data->vscroller_object, MUIA_Prop_First, data->vert_first);

    SetAttrs(data->overview_object,
	     MUIA_NoNotify, TRUE,
	     MUIA_Overview_RectLeft, data->horiz_first,
	     MUIA_Overview_RectTop, data->vert_first,
	     TAG_DONE);

    data->update = 3;
    MUI_Redraw(o, MADF_DRAWUPDATE);
  }

  return DoSuperMethodA(cl, o, (Msg) msg);
}

STATIC ULONG Map_Get(struct IClass * cl, Object * o, struct opGet * msg)
{
  struct Map_Data *data = (struct Map_Data *) INST_DATA(cl, o);
  switch (msg->opg_AttrID)
  {
  case MUIA_Map_HorizVisible:
    if (NORMAL_TILE_WIDTH && data->shown)
    {
      *msg->opg_Storage = (_mwidth(o) + NORMAL_TILE_WIDTH - 1) / NORMAL_TILE_WIDTH;
    }
    else
      *msg->opg_Storage = 0;
    break;

  case MUIA_Map_VertVisible:
    if (NORMAL_TILE_HEIGHT && data->shown)
    {
      *msg->opg_Storage = (_mheight(o) + NORMAL_TILE_HEIGHT - 1) / NORMAL_TILE_HEIGHT;
    }
    else
      *msg->opg_Storage = 0;
    break;

  case MUIA_Map_HorizFirst:
    *msg->opg_Storage = data->horiz_first;
    break;

  case MUIA_Map_VertFirst:
    *msg->opg_Storage = data->vert_first;
    break;

  default:
    return DoSuperMethodA(cl, o, (Msg) msg);
  }
  return 1;
}

STATIC ULONG Map_Setup(struct IClass * cl, Object * o, Msg msg)
{
  struct Map_Data *data = (struct Map_Data *) INST_DATA(cl, o);
  struct ColorMap *cm;
  APTR dh;

  if (!DoSuperMethodA(cl, o, msg))
    return FALSE;

  cm = _screen(o)->ViewPort.ColorMap;
  data->m_black = ObtainBestPenA(cm, 0, 0, 0, NULL);

  data->intro_gfx_sprite = load_sprite(main_intro_filename, FALSE);
  data->radar_gfx_sprite = load_sprite(minimap_intro_filename, FALSE);

  if ((dh = data->drawhandle = ObtainDrawHandle(pen_shared_map, &_screen(o)->RastPort, _screen(o)->ViewPort.ColorMap,
					   GGFX_DitherMode, DITHERMODE_NONE,
						TAG_DONE)))
  {
    if (data->overview_object)
      set(data->overview_object, MUIA_Overview_RadarPicture, data->radar_gfx_sprite->picture);

    if ((data->unit_bitmap = AllocBitMap(get_normal_tile_width() + 4, get_normal_tile_height() + 4, GetBitMapAttr(_screen(o)->RastPort.BitMap, BMA_DEPTH), BMF_MINPLANES, _screen(o)->RastPort.BitMap)))
    {
      if ((data->unit_layerinfo = NewLayerInfo()))
      {
	InstallLayerInfoHook(data->unit_layerinfo, LAYERS_NOBACKFILL);
	if ((data->unit_layer = CreateBehindHookLayer(data->unit_layerinfo, data->unit_bitmap, 0, 0, get_normal_tile_width() + 4 - 1, get_normal_tile_height() + 4 - 1, LAYERSIMPLE, LAYERS_NOBACKFILL, NULL)))
	{
	  if (render_sprites( /*_screen(o),*/ dh))
	  {
	    MUI_RequestIDCMP(o, IDCMP_MOUSEBUTTONS);
	    return TRUE;
	  }
	}
      }
    }
  }

  CoerceMethod(cl, o, MUIM_Cleanup);
  return FALSE;
}

STATIC ULONG Map_Cleanup(struct IClass * cl, Object * o, Msg msg)
{
  struct Map_Data *data = (struct Map_Data *) INST_DATA(cl, o);
  struct ColorMap *cm;

  MUI_RejectIDCMP(o, IDCMP_MOUSEBUTTONS);

  if (data->overview_object)
    set(data->overview_object, MUIA_Overview_RadarPicture, NULL);

  if (data->radar_gfx_sprite)
  {
    real_free_sprite(data->radar_gfx_sprite);
    data->radar_gfx_sprite = NULL;
  }
  if (data->intro_gfx_sprite)
  {
    real_free_sprite(data->intro_gfx_sprite);
    data->intro_gfx_sprite = NULL;
  }

  cleanup_sprites();
  if (data->drawhandle)
  {
    ReleaseDrawHandle(data->drawhandle);
    data->drawhandle = NULL;
  }
  if (data->unit_layer)
  {
    DeleteLayer(NULL, data->unit_layer);
    data->unit_layer = NULL;
    WaitBlit();
  }
  if (data->unit_layerinfo)
  {
    DisposeLayerInfo(data->unit_layerinfo);
    data->unit_layerinfo = NULL;
  }
  if (data->unit_bitmap)
  {
    FreeBitMap(data->unit_bitmap);
    data->unit_bitmap = NULL;
  }

  cm = _screen(o)->ViewPort.ColorMap;
  ReleasePen(cm, data->m_black);

  return DoSuperMethodA(cl, o, msg);
}

STATIC ULONG Map_AskMinMax(struct IClass * cl, Object * o, struct MUIP_AskMinMax * msg)
{
  struct Map_Data *data = (struct Map_Data *) INST_DATA(cl, o);
  DoSuperMethodA(cl, o, (Msg) msg);

  msg->MinMaxInfo->MinWidth += 2;
  msg->MinMaxInfo->DefWidth += 2;	//get_normal_tile_height()*5;

  msg->MinMaxInfo->MaxWidth += MUI_MAXMAX;

  msg->MinMaxInfo->MinHeight += 2;
  msg->MinMaxInfo->DefHeight += 2;	//get_normal_tile_height()*5;

  msg->MinMaxInfo->MaxHeight += MUI_MAXMAX;
  return 0;
}

STATIC ULONG Map_Show(struct IClass * cl, Object * o, Msg msg)
{
  struct Map_Data *data = (struct Map_Data *) INST_DATA(cl, o);
  static int test = 0;
  ULONG flags;
  ULONG depth;

  DoSuperMethodA(cl, o, msg);
  data->shown = TRUE;

  flags = (GetBitMapAttr(_screen(o)->RastPort.BitMap, BMA_FLAGS) & BMF_STANDARD) ? NULL : BMF_MINPLANES;
  depth = GetBitMapAttr(_screen(o)->RastPort.BitMap, BMA_DEPTH);

  if (data->map_bitmap = AllocBitMap(_mwidth(o), _mheight(o), depth, flags, _screen(o)->RastPort.BitMap))
  {
    if ((data->map_layerinfo = NewLayerInfo()))
    {
      InstallLayerInfoHook(data->map_layerinfo, LAYERS_NOBACKFILL);
      if ((data->map_layer = CreateBehindHookLayer(data->map_layerinfo, data->map_bitmap, 0, 0, _mwidth(o) - 1, _mheight(o) - 1, LAYERSIMPLE, LAYERS_NOBACKFILL, NULL)))
      {
	if (data->hscroller_object)
	{
	  SetAttrs(data->hscroller_object,
	  MUIA_Prop_Entries, map.xsize + xget(o, MUIA_Map_HorizVisible) - 1,
		   MUIA_Prop_Visible, xget(o, MUIA_Map_HorizVisible),
		   MUIA_NoNotify, TRUE,
		   TAG_DONE);
	}

	if (data->vscroller_object)
	{
	  SetAttrs(data->vscroller_object,
		   MUIA_Prop_Entries, map.ysize,
		   MUIA_Prop_Visible, xget(o, MUIA_Map_VertVisible),
		   MUIA_NoNotify, TRUE,
		   TAG_DONE);
	}

	if (data->overview_object)
	{
	  SetAttrs(data->overview_object,
		   MUIA_Overview_RectWidth, xget(o, MUIA_Map_HorizVisible),
		   MUIA_Overview_RectHeight, xget(o, MUIA_Map_VertVisible),
		   MUIA_NoNotify, TRUE,
		   TAG_DONE);
	}
	return 1;
      }
    }
  }
  CoerceMethod(cl, o, MUIM_Hide);
  return 0;
}

STATIC ULONG Map_Hide(struct IClass * cl, Object * o, Msg msg)
{
  struct Map_Data *data = (struct Map_Data *) INST_DATA(cl, o);
  if (data->map_layer)
  {
    DeleteLayer(NULL, data->map_layer);
    data->map_layer = NULL;
    WaitBlit();			/* DeleteLayer() may use the blitter */
  }
  if (data->map_layerinfo)
  {
    DisposeLayerInfo(data->map_layerinfo);
    data->map_layerinfo = NULL;
  }
  if (data->map_bitmap)
  {
    FreeBitMap(data->map_bitmap);
    data->map_bitmap = NULL;
  }

  data->shown = FALSE;

  return DoSuperMethodA(cl, o, (Msg) msg);
}

STATIC ULONG Map_Draw(struct IClass * cl, Object * o, struct MUIP_Draw * msg)
{
  struct Map_Data *data = (struct Map_Data *) INST_DATA(cl, o);
  struct RastPort *rp = _rp(o);

  DoSuperMethodA(cl, o, (Msg) msg);

  if (get_client_state() == CLIENT_GAME_RUNNING_STATE)
  {
    BOOL drawmap = FALSE;
    if (msg->flags & MADF_DRAWUPDATE)
    {
      if (data->update == 2)
      {
/*	Move the Unit Smoothly (from mapview.c) */
	int map_view_x0 = data->horiz_first;
	int map_view_y0 = data->vert_first;
	int i, x, y, diffx, diffy, x0 = data->x0, y0 = data->y0, dx = data->dx,
	  dy = data->dy, dest_x = data->dest_x, dest_y = data->dest_y;
	struct unit *punit = data->punit;
	int start_x;

	if (x0 >= map_view_x0)
	  x = (x0 - map_view_x0) * get_normal_tile_width();
	else
	  x = (map.xsize - map_view_x0 + x0) * get_normal_tile_width();

	y = (y0 - map_view_y0) * get_normal_tile_height();

	diffx = get_normal_tile_width();
	diffy = get_normal_tile_height();

	for (i = 0; i < get_normal_tile_width() / 2; i++)
	{
	  LONG x1, y1, w, h, ox, oy;
	  TimeDelay(0, 0, 1000000 / 65);

	  x += dx * 2;
	  y += dy * 2;
	  diffx += dx * 2;
	  diffy += dy * 2;

	  x1 = x - 2;
	  y1 = y - 2;
	  w = get_normal_tile_width() + 4;
	  h = get_normal_tile_height() + 4;
	  ox = oy = 0;

	  if (x1 < 0)
	  {
	    ox = -x1;
	    w += x1;
	    x1 = 0;
	  }

	  if (y1 < 0)
	  {
	    oy = -y1;
	    h += y1;
	    y1 = 0;
	  }

	  if (x1 + w > _mwidth(o))
	    w = _mwidth(o) - x1;
	  if (y1 + h > _mheight(o))
	    h = _mheight(o) - y1;

	  if (w > 0 && h > 0)
	  {
	    BltBitMapRastPort(data->map_bitmap, x1, y1,
			      data->unit_layer->rp, ox, oy, w, h, 0xc0);

	    put_unit_tile(data->unit_layer->rp, punit, 2, 2);

	    BltBitMapRastPort(data->unit_bitmap, ox, oy,
			 _rp(o), _mleft(o) + x1, _mtop(o) + y1, w, h, 0xc0);
	  }
	}
      }
      else
	drawmap = TRUE;
    }
    else
      drawmap = TRUE;

    if (drawmap)
    {
      LONG tile_x;
      LONG tile_y;
      LONG width;
      LONG height;

      LONG map_view_x0 = xget(o, MUIA_Map_HorizFirst);
      LONG map_view_y0 = xget(o, MUIA_Map_VertFirst);

      int x, y;
      BOOL bltall = FALSE;
      BOOL citynames = FALSE;
      BOOL need_put = TRUE;

      if ((msg->flags & MADF_DRAWUPDATE) && (data->update == 1))
      {
	tile_x = data->tilex;
	tile_y = data->tiley;
	width = data->width;
	height = data->height;

	if (width > 1 && height > 1)
	  citynames = TRUE;
      }
      else
      {
	tile_x = 0;
	tile_y = 0;
	width = xget(o, MUIA_Map_HorizVisible);
	height = xget(o, MUIA_Map_VertVisible);

	citynames = TRUE;

	if ((msg->flags & MADF_DRAWUPDATE) && (data->update == 3))
	{
	  LONG dx = data->horiz_first - data->old_horiz_first;
	  LONG dy = data->vert_first - data->old_vert_first;
	  if (abs(dx) < width && abs(dy) < height)
	  {
	    ScrollRaster(data->map_layer->rp, dx * get_normal_tile_width(), dy * get_normal_tile_height(),
			 0, 0, _mwidth(o) - 1, _mheight(o) - 1);

	    bltall = TRUE;

	    if (abs(dx) < width && dx)
	    {
	      if (dx > 0)
	      {
		tile_x = width - dx - 1;
		if (tile_x < 0)
		  tile_x = 0;
		width = dx + 1;
	      }
	      else
		width = -dx;
	    }

	    if (abs(dy) < height && dy)
	    {
	      if (dy > 0)
	      {
		tile_y = height - dy - 1;
		if (tile_y < 0)
		  tile_y = 0;
		height = dy + 1;
	      }
	      else
		height = -dy;
	    }

	    if (dx && dy && abs(dx) < xget(o, MUIA_Map_HorizVisible) && abs(dy) < xget(o, MUIA_Map_VertVisible))
	    {
	      LONG newwidth = xget(o, MUIA_Map_HorizVisible);
	      need_put = FALSE;

	      if ((dy > 0 && dx > 0) || (dy < 0 && dx > 0))	/* dy > 0 = Nach unten */
	      {
		for (y = tile_y; y < tile_y + height; y++)
		{
		  for (x = 0; x < newwidth - width; x++)
		    put_tile(data->map_layer->rp, 0, 0, x, y, map_view_x0 + x, map_view_y0 + y, 0);
		}
	      }
	      else
	      {
		for (y = tile_y; y < tile_y + height; y++)
		{
		  for (x = tile_x; x < newwidth; x++)
		    put_tile(data->map_layer->rp, 0, 0, x, y, map_view_x0 + x, map_view_y0 + y, 0);
		}
	      }

	      height = xget(o, MUIA_Map_VertVisible);

	      for (y = 0; y < height; y++)
		for (x = tile_x; x < tile_x + width; x++)
		  put_tile(data->map_layer->rp, 0, 0, x, y, map_view_x0 + x, map_view_y0 + y, 0);
	    }
	  }
	}
      }


      if (need_put)
      {
	for (y = tile_y; y < tile_y + height; y++)
	  for (x = tile_x; x < tile_x + width; x++)
	    put_tile(data->map_layer->rp, 0, 0, x, y, map_view_x0 + x, map_view_y0 + y, 0);
      }

      x = tile_x * get_normal_tile_width();
      y = tile_y * get_normal_tile_height();

      if (!(data->update == 1 && !data->write_to_screen) || data->update != 4)
      {
	if (bltall)
	{
	  BltBitMapRastPort(data->map_bitmap, 0, 0,
		_rp(o), _mleft(o), _mtop(o), _mwidth(o), _mheight(o), 0xc0);
	}
	else
	{
	  /* Own simple cliping is a lot of faster */
	  LONG pix_width = width * get_normal_tile_width();
	  LONG pix_height = height * get_normal_tile_height();

	  if (pix_width + x > _mwidth(o))
	    pix_width = _mwidth(o) - x;
	  if (pix_height + y > _mheight(o))
	    pix_height = _mheight(o) - y;

	  if (pix_width > 0 && pix_height > 0)
	  {
	    BltBitMapRastPort(data->map_bitmap, x, y,
			      _rp(o), _mleft(o) + x, _mtop(o) + y,
			      pix_width, pix_height, 0xc0);
	  }
	}
      }

      if (citynames)
      {
	struct TextFont *new_font;
	new_font = _font(o);
	if (new_font)
	{
	  struct TextFont *org_font;
	  APTR cliphandle = MUI_AddClipping(muiRenderInfo(o), _mleft(o), _mtop(o), _mwidth(o), _mheight(o));

	  width = xget(o, MUIA_Map_HorizVisible);
	  height = xget(o, MUIA_Map_VertVisible);

	  GetRPAttrs(rp, RPTAG_Font, &org_font, TAG_DONE);
	  SetFont(rp, new_font);

	  for (y = 0; y < height; ++y)
	  {
	    int ry = map_view_y0 + y;
	    for (x = 0; x < width; ++x)
	    {
	      int rx = (map_view_x0 + x) % map.xsize;
	      struct city *pcity;
	      if ((pcity = map_get_city(rx, ry)))
	      {
		LONG w = TextLength(rp, pcity->name, strlen(pcity->name));
		LONG pix_x = _mleft(o) + x * NORMAL_TILE_WIDTH + NORMAL_TILE_WIDTH / 2 - w / 2 + 1;
		LONG pix_y = _mtop(o) + (y + 1) * NORMAL_TILE_HEIGHT + 1 + rp->TxBaseline - 2;
		SetAPen(rp, 1);	/* black */
		Move(rp, pix_x, pix_y);
		Text(rp, pcity->name, strlen(pcity->name));
		SetAPen(rp, 2);	/* white */
		Move(rp, pix_x - 1, pix_y - 1);
		Text(rp, pcity->name, strlen(pcity->name));
	      }
	    }
	  }

	  MUI_RemoveClipping(muiRenderInfo(o), cliphandle);

	  SetFont(rp, org_font);
	}
      }
    }

    data->update = 0;
  }
  else
  {
    if (data->intro_gfx_sprite && data->intro_gfx_sprite->picture)
    {
      APTR dh = ObtainDrawHandleA(pen_shared_map, _rp(o), _screen(o)->ViewPort.ColorMap, NULL);
      if (dh)
      {
	DrawPicture(dh, data->intro_gfx_sprite->picture, _mleft(o), _mtop(o),
		    GGFX_DestWidth, _mwidth(o),
		    GGFX_DestHeight, _mheight(o),
		    TAG_DONE);

	ReleaseDrawHandle(dh);
      }
    }
  }
  return 0;
}

STATIC ULONG Map_HandleInput(struct IClass * cl, Object * o, struct MUIP_HandleInput * msg)
{
  struct Map_Data *data = (struct Map_Data *) INST_DATA(cl, o);

  switch (msg->muikey)
  {
    case MUIKEY_LEFT:
      if (data->hscroller_object)
        set (data->hscroller_object, MUIA_Prop_First, xget(data->hscroller_object, MUIA_Prop_First)-1);
      break;

    case MUIKEY_RIGHT:
      if (data->hscroller_object)
        set (data->hscroller_object, MUIA_Prop_First, xget(data->hscroller_object, MUIA_Prop_First)+1);
      break;

    case MUIKEY_UP:
      if (data->vscroller_object)
        set (data->vscroller_object, MUIA_Prop_First, xget(data->vscroller_object, MUIA_Prop_First)-1);
      break;

    case MUIKEY_DOWN:
      if (data->vscroller_object)
        set (data->vscroller_object, MUIA_Prop_First, xget(data->vscroller_object, MUIA_Prop_First)+1);
      break;
  }


  if (msg->imsg)
  {
    UWORD qual = msg->imsg->Qualifier;

    switch (msg->imsg->Class)
    {
    case IDCMP_MOUSEBUTTONS:
      if (msg->imsg->Code == SELECTDOWN)
      {
	if (_isinobject(msg->imsg->MouseX, msg->imsg->MouseY))
	{
	  LONG x = msg->imsg->MouseX - _mleft(o);
	  LONG y = msg->imsg->MouseY - _mtop(o);
	  x /= get_normal_tile_width();
	  y /= get_normal_tile_height();
	  x += data->horiz_first;
	  y += data->vert_first;

	  if ((qual & IEQUALIFIER_LSHIFT) || (qual & IEQUALIFIER_RSHIFT))
	  {
	    if (!data->pop_wnd)
	    {
	      data->pop_wnd = TilePopWindowObject,
		MUIA_TilePopWindow_X, x,
		MUIA_TilePopWindow_Y, y,
		End;

	      if (data->pop_wnd)
	      {
		DoMethod(_app(o), OM_ADDMEMBER, data->pop_wnd);
		set(data->pop_wnd, MUIA_Window_Open, TRUE);
	      }
	    }
	  }
	  else
	  {
	    data->click.x = map_adjust_x(x);
	    data->click.y = map_adjust_y(y);

	    set(o, MUIA_Map_Click, &data->click);
	  }
	}
      }
      else
      {
	if (msg->imsg->Code == SELECTUP)
	{
	  if (data->pop_wnd)
	  {
	    set(data->pop_wnd, MUIA_Window_Open, FALSE);
	    DoMethod(_app(o), OM_REMMEMBER, data->pop_wnd);
	    MUI_DisposeObject(data->pop_wnd);
	    data->pop_wnd = NULL;
	  }
	}
      }

      break;
    }
  }
  return 0;
}

STATIC ULONG Map_ContextMenuBuild(struct IClass * cl, Object * o, struct MUIP_ContextMenuBuild * msg)
{
  struct Map_Data *data = (struct Map_Data *) INST_DATA(cl, o);
  Object *context_menu = NULL;

  if (data->context_menu)
    MUI_DisposeObject(data->context_menu);

  if (get_client_state() == CLIENT_GAME_RUNNING_STATE)
  {
    if (_isinobject(msg->mx, msg->my))
    {
      struct city *pcity;
      struct unit *punit, *focus;
      struct tile *ptile;

      LONG x = msg->mx - _mleft(o);
      LONG y = msg->my - _mtop(o);
      x /= get_normal_tile_width();
      y /= get_normal_tile_height();
      x += data->horiz_first;
      y += data->vert_first;

      ptile = map_get_tile(x, y);

      if (ptile->known >= TILE_KNOWN)
      {
	Object *menu_title;
	static char title[256];

	pcity = map_get_city(x, y);
	punit = player_find_visible_unit(game.player_ptr, ptile);
	focus = get_unit_in_focus();

	if (pcity)
	  sprintf(title, "City %s", pcity->name);
	else if (punit)
	  sprintf(title, "Unit %s", unit_name(punit->type));
	else
	  sprintf(title, "Tile %s", map_get_tile_info_text(x, y));

	context_menu = MenustripObject,
	  Child, menu_title = MenuObjectT(title),
	  End,
	  End;

	if (context_menu)
	{
	  BOOL need_barlabel = FALSE;

	  Object *entry;
	  if (pcity)
	  {
	    Map_MakeContextItem(menu_title, "Popup City", PACK_CITY_USERDATA(pcity, CITY_POPUP));
	    need_barlabel = TRUE;
	  }
	  else
	  {
	    if (punit)
	    {
	      struct Command_List list;
	      NewList((struct List *) &list);
	      if ((list.pool = CreatePool(0, 1024, 1024)))
	      {
	      	/* Note: Must be better done (linked with the pull down menu */
		if (can_unit_build_city(punit))
		  Map_InsertCommand(&list, "Build City", PACK_USERDATA(punit, MENU_ORDER_BUILD_CITY));
		if (can_unit_add_to_city(punit))
		  Map_InsertCommand(&list, "Add to City", PACK_USERDATA(punit, MENU_ORDER_BUILD_CITY));
		if (can_unit_do_activity(punit, ACTIVITY_ROAD))
		  Map_InsertCommand(&list, "Build Road", PACK_USERDATA(punit, MENU_ORDER_ROAD));
		if (can_unit_do_activity(punit, ACTIVITY_RAILROAD))
		  Map_InsertCommand(&list, "Build Railroad", PACK_USERDATA(punit, MENU_ORDER_ROAD));
		if (can_unit_do_activity(punit, ACTIVITY_FORTRESS))
		  Map_InsertCommand(&list, "Build Fortress", PACK_USERDATA(punit, MENU_ORDER_FORTRESS));
		if (can_unit_do_activity(punit, ACTIVITY_AIRBASE))
		  Map_InsertCommand(&list, "Build Airbase", PACK_USERDATA(punit, MENU_ORDER_AIRBASE));
		if (can_unit_do_activity(punit, ACTIVITY_POLLUTION))
		  Map_InsertCommand(&list, "Clean Pollution", PACK_USERDATA(punit, MENU_ORDER_POLLUTION));
		if (can_unit_do_activity(punit, ACTIVITY_FORTIFY))
		  Map_InsertCommand(&list, "Fortify", PACK_USERDATA(punit, MENU_ORDER_FORTIFY));
		if (can_unit_do_activity(punit, ACTIVITY_SENTRY))
		  Map_InsertCommand(&list, "Sentry", PACK_USERDATA(punit, MENU_ORDER_SENTRY));
		if (can_unit_do_activity(punit, ACTIVITY_PILLAGE))
		  Map_InsertCommand(&list, "Pillage", PACK_USERDATA(punit, MENU_ORDER_PILLAGE));
		if (can_unit_do_auto(punit) && unit_flag(punit->type, F_SETTLERS))
		  Map_InsertCommand(&list, "Auto Settler", PACK_USERDATA(punit, MENU_ORDER_AUTO_SETTLER));
		if (can_unit_do_auto(punit) && !unit_flag(punit->type, F_SETTLERS))
		  Map_InsertCommand(&list, "Auto Attack", PACK_USERDATA(punit, MENU_ORDER_AUTO_ATTACK));
		if (can_unit_do_activity(punit, ACTIVITY_EXPLORE))
		  Map_InsertCommand(&list, "Auto Explore", PACK_USERDATA(punit, MENU_ORDER_AUTO_EXPLORE));
		if (can_unit_paradropped(punit))
		  Map_InsertCommand(&list, "Paradrop", PACK_USERDATA(punit, MENU_ORDER_POLLUTION));
		if (unit_flag(punit->type, F_NUCLEAR))
		  Map_InsertCommand(&list, "Explode Nuclear", PACK_USERDATA(punit, MENU_ORDER_NUKE));
		if (get_transporter_capacity(punit) > 0)
		  Map_InsertCommand(&list, "Unload", PACK_USERDATA(punit, MENU_ORDER_UNLOAD));
		if (is_unit_activity_on_tile(ACTIVITY_SENTRY, punit->x, punit->y))
		  Map_InsertCommand(&list, "Wake up", PACK_USERDATA(punit, MENU_ORDER_WAKEUP_OTHERS));
		if (punit != focus)
		  Map_InsertCommand(&list, "Activate", PACK_USERDATA(punit, UNIT_ACTIVATE));

		if (can_unit_do_activity(punit, ACTIVITY_IRRIGATE))
		{
		  static char irrtext[64];
		  if ((map_get_tile(punit->x, punit->y)->special & S_IRRIGATION) && improvement_exists(B_SUPERMARKET))
		  {
		    strcpy(irrtext, "Build Farmland");
		  }
		  else
		    strcpy(irrtext, "Build Irrigation");
		  Map_InsertCommand(&list, irrtext, PACK_USERDATA(punit, MENU_ORDER_IRRIGATE));
		}

		if (can_unit_do_activity(punit, ACTIVITY_MINE))
		{
		  static char mintext[64];
		  strcpy(mintext, "Build Mine");
		  Map_InsertCommand(&list, mintext, PACK_USERDATA(punit, MENU_ORDER_MINE));
		}

		if (can_unit_do_activity(punit, ACTIVITY_TRANSFORM))
		{
		  static char transtext[64];
		  strcpy(transtext, "Transform terrain");
		  Map_InsertCommand(&list, transtext, PACK_USERDATA(punit, MENU_ORDER_TRANSFORM));
		}

		if (!IsListEmpty((struct List *) &list))
		{
		  struct Command_Node *node;

		  Command_List_Sort(&list);

		  while ((node = (struct Command_Node *) RemHead((struct List *) &list)))
		  {
		    Map_MakeContextItem(menu_title, node->name, node->id);
		  }
		  need_barlabel = TRUE;
		}

		DeletePool(list.pool);
	      }
	    }
	  }

	  if (unit_list_size(&ptile->units) > 1 && punit)
	  {
	    if (need_barlabel)
	    {
	      Map_MakeContextBarlabel(menu_title);
	    }

	    Map_MakeContextItem(menu_title, "List all units", PACK_USERDATA(punit, UNIT_POPUP_UNITLIST));
	    need_barlabel = TRUE;
	  }

	  if (focus)
	  {
	    if (punit != focus)
	    {
	      if (need_barlabel)
	      {
		Map_MakeContextBarlabel(menu_title);
	      }
	      Map_MakeContextItem(menu_title, "Goto this location", PACK_USERDATA(focus, UNIT_GOTOLOC));

	      data->click.x = x;
	      data->click.y = y;
	    }
	  }
	}
      }
    }
  }

  data->context_menu = context_menu;

  return (ULONG) context_menu;
}

STATIC ULONG Map_ContextMenuChoice(struct IClass * cl, Object * o, struct MUIP_ContextMenuChoice * msg)
{
  struct Map_Data *data = (struct Map_Data *) INST_DATA(cl, o);
  ULONG udata = muiUserData(msg->item);
  ULONG command = UNPACK_COMMAND(udata);

  switch (command)
  {
  case CITY_POPUP:
    {
      struct city *pcity = UNPACK_CITY(udata);
      if (pcity)
      {
	popup_city_dialog(pcity, 0);
      }
    }
    break;

  case UNIT_GOTOLOC:
    {
      struct unit *punit = UNPACK_UNIT(udata);
      if (punit)
      {
	request_unit_goto_location(punit, data->click.x, data->click.y);
      }
    }
    break;

  default:
    {
      struct unit *punit = UNPACK_UNIT(udata);
      if (punit)
      {
	do_unit_function(punit, command);
      }
    }
    break;
  }
  return (ULONG) NULL;
}

STATIC ULONG Map_Refresh(struct IClass * cl, Object * o, struct MUIP_Map_Refresh * msg)
{
  struct Map_Data *data = (struct Map_Data *) INST_DATA(cl, o);

  data->update = 1;
  data->tilex = msg->tilex;
  data->tiley = msg->tiley;
  data->width = msg->width;
  data->height = msg->height;
  data->write_to_screen = msg->write_to_screen;

  if (data->shown)
    MUI_Redraw(o, MADF_DRAWUPDATE);
  return 0;
}

STATIC ULONG Map_MoveUnit(struct IClass * cl, Object * o, struct MUIP_Map_MoveUnit * msg)
{
  struct Map_Data *data = (struct Map_Data *) INST_DATA(cl, o);

  data->punit = msg->punit;
  data->x0 = msg->x0;
  data->y0 = msg->y0;
  data->dx = msg->dx;
  data->dy = msg->dy;
  data->dest_x = msg->dest_x;
  data->dest_y = msg->dest_y;
  data->update = 2;

  MUI_Redraw(o, MADF_DRAWUPDATE);
  return 0;
}

STATIC ULONG Map_ShowCityNames(struct IClass * cl, Object * o, Msg msg)
{
  struct Map_Data *data = (struct Map_Data *) INST_DATA(cl, o);
  data->update = 4;
  MUI_Redraw(o, MADF_DRAWUPDATE);
  return 0;
}

STATIC ULONG Map_PutCityWorkers(struct IClass * cl, Object * o, struct MUIP_Map_PutCityWorkers * msg)
{
  return NULL;
}

STATIC ULONG Map_PutCrossTile(struct IClass * cl, Object * o, struct MUIP_Map_PutCrossTile * msg)
{
  return NULL;
}

STATIC __asm __saveds ULONG Map_Dispatcher(register __a0 struct IClass * cl, register __a2 Object * obj, register __a1 Msg msg)
{
  switch (msg->MethodID)
  {
  case OM_NEW:
    return Map_New(cl, obj, (struct opSet *) msg);
  case OM_DISPOSE:
    return Map_Dispose(cl, obj, msg);
  case OM_GET:
    return Map_Get(cl, obj, (struct opGet *) msg);
  case OM_SET:
    return Map_Set(cl, obj, (struct opSet *) msg);
  case MUIM_Setup:
    return Map_Setup(cl, obj, msg);
  case MUIM_Cleanup:
    return Map_Cleanup(cl, obj, msg);
  case MUIM_AskMinMax:
    return Map_AskMinMax(cl, obj, (struct MUIP_AskMinMax *) msg);
  case MUIM_Show:
    return Map_Show(cl, obj, msg);
  case MUIM_Hide:
    return Map_Hide(cl, obj, msg);
  case MUIM_Draw:
    return Map_Draw(cl, obj, (struct MUIP_Draw *) msg);
  case MUIM_HandleInput:
    return Map_HandleInput(cl, obj, (struct MUIP_HandleInput *) msg);
  case MUIM_ContextMenuBuild:
    return Map_ContextMenuBuild(cl, obj, (struct MUIP_ContextMenuBuild *) msg);
  case MUIM_ContextMenuChoice:
    return Map_ContextMenuChoice(cl, obj, (struct MUIP_ContextMenuChoice *) msg);

  case MUIM_Map_Refresh:
    return Map_Refresh(cl, obj, (struct MUIP_Map_Refresh *) msg);
  case MUIM_Map_MoveUnit:
    return Map_MoveUnit(cl, obj, (struct MUIP_Map_MoveUnit *) msg);
  case MUIM_Map_ShowCityNames:
    return Map_ShowCityNames(cl, obj, msg);
  case MUIM_Map_PutCityWorkers:
    return Map_PutCityWorkers(cl, obj, (struct MUIP_Map_PutCityWorkers *) msg);
  case MUIM_Map_PutCrossTile:
    return Map_PutCrossTile(cl, obj, (struct MUIP_Map_PutCrossTile *) msg);
  }

  return (DoSuperMethodA(cl, obj, msg));
}

/****************************************************************
 CityMap Custom Class
*****************************************************************/

struct MUI_CustomClass *CL_CityMap;

Object *MakeCityMap(struct city *pcity)
{
  return CityMapObject,
    MUIA_CityMap_City, pcity,
    End;
}

struct CityMap_Data
{
  struct city *pcity;
  struct Map_Click click;	/* Clicked Tile on the Map */

  ULONG red_color;
};


STATIC ULONG CityMap_New(struct IClass *cl, Object * o, struct opSet *msg)
{
  if ((o = (Object *) DoSuperMethodA(cl, o, (Msg) msg)))
  {
    struct CityMap_Data *data = (struct CityMap_Data *) INST_DATA(cl, o);
    struct TagItem *tl = msg->ops_AttrList;
    struct TagItem *ti;

    while ((ti = NextTagItem(&tl)))
    {
      switch (ti->ti_Tag)
      {
      case MUIA_CityMap_City:
	data->pcity = (struct city *) ti->ti_Data;
	break;
      }
    }
  }
  return (ULONG) o;
}

STATIC ULONG CityMap_Dispose(struct IClass * cl, Object * o, Msg msg)
{
  struct CityMap_Data *data = (struct CityMap_Data *) INST_DATA(cl, o);
  return DoSuperMethodA(cl, o, msg);
}

STATIC ULONG CityMap_Set(struct IClass * cl, Object * o, struct opSet * msg)
{
  struct CityMap_Data *data = (struct CityMap_Data *) INST_DATA(cl, o);
  struct TagItem *tl = msg->ops_AttrList;
  struct TagItem *ti;

  while ((ti = NextTagItem(&tl)))
  {
    switch (ti->ti_Tag)
    {
    case MUIA_CityMap_City:
      {
	struct city *newcity = (struct city *) ti->ti_Data;
	if (newcity != data->pcity)
	{
	  data->pcity = newcity;
//                                                              MUI_Redraw(o,MADF_DRAWUPDATE);
	}
      }
      break;
    }
  }

  return DoSuperMethodA(cl, o, (Msg) msg);
}

STATIC ULONG CityMap_Get(struct IClass * cl, Object * o, struct opGet * msg)
{
  struct CityMap_Data *data = (struct CityMap_Data *) INST_DATA(cl, o);
  switch (msg->opg_AttrID)
  {
  default:
    return DoSuperMethodA(cl, o, (Msg) msg);
  }
  return 1;
}

STATIC ULONG CityMap_Setup(struct IClass * cl, Object * o, Msg msg)
{
  struct CityMap_Data *data = (struct CityMap_Data *) INST_DATA(cl, o);
  struct ColorMap *cm;

  if (!DoSuperMethodA(cl, o, msg))
    return FALSE;

  cm = _screen(o)->ViewPort.ColorMap;
  data->red_color = ObtainBestPenA(cm, 0xffffffff, 0, 0, NULL);

  MUI_RequestIDCMP(o, IDCMP_MOUSEBUTTONS);

  return TRUE;
}

STATIC ULONG CityMap_Cleanup(struct IClass * cl, Object * o, Msg msg)
{
  struct CityMap_Data *data = (struct CityMap_Data *) INST_DATA(cl, o);
  struct ColorMap *cm;
  cm = _screen(o)->ViewPort.ColorMap;

  MUI_RejectIDCMP(o, IDCMP_MOUSEBUTTONS);

  ReleasePen(cm, data->red_color);

  DoSuperMethodA(cl, o, msg);
  return 0;
}

STATIC ULONG CityMap_AskMinMax(struct IClass * cl, Object * o, struct MUIP_AskMinMax * msg)
{
  struct CityMap_Data *data = (struct CityMap_Data *) INST_DATA(cl, o);
  DoSuperMethodA(cl, o, (Msg) msg);

  msg->MinMaxInfo->MinWidth += get_normal_tile_width() * 5;
  msg->MinMaxInfo->DefWidth += get_normal_tile_width() * 5;
  msg->MinMaxInfo->MaxWidth += get_normal_tile_width() * 5;

  msg->MinMaxInfo->MinHeight += get_normal_tile_height() * 5;
  msg->MinMaxInfo->DefHeight += get_normal_tile_height() * 5;
  msg->MinMaxInfo->MaxHeight += get_normal_tile_height() * 5;
  return 0;
}

STATIC ULONG CityMap_Show(struct IClass * cl, Object * o, Msg msg)
{
  struct CityMap_Data *data = (struct CityMap_Data *) INST_DATA(cl, o);
  DoSuperMethodA(cl, o, msg);
  return 1;
}

STATIC ULONG CityMap_Draw(struct IClass * cl, Object * o, struct MUIP_Draw * msg)
{
  struct CityMap_Data *data = (struct CityMap_Data *) INST_DATA(cl, o);
  struct RastPort *rp = _rp(o);

  DoSuperMethodA(cl, o, (Msg) msg);

  {
    int x, y;
    struct city *pcity = data->pcity;

    for (y = 0; y < CITY_MAP_SIZE; y++)
    {
      for (x = 0; x < CITY_MAP_SIZE; x++)
      {
	if (!(x == 0 && y == 0) && !(x == 0 && y == CITY_MAP_SIZE - 1) &&
	    !(x == CITY_MAP_SIZE - 1 && y == 0) &&
	    !(x == CITY_MAP_SIZE - 1 && y == CITY_MAP_SIZE - 1) &&
	    tile_is_known(pcity->x + x - CITY_MAP_SIZE / 2,
			  pcity->y + y - CITY_MAP_SIZE / 2))
	{
	  put_tile(_rp(o), _mleft(o), _mtop(o), x, y,
		   pcity->x + x - CITY_MAP_SIZE / 2,
		   pcity->y + y - CITY_MAP_SIZE / 2, 1);

	  if (pcity->city_map[x][y] == C_TILE_WORKER)
	  {
	    put_city_output_tile(_rp(o), get_food_tile(x, y, pcity), get_shields_tile(x, y, pcity), get_trade_tile(x, y, pcity),
				 _mleft(o), _mtop(o), x, y);

	  }
	  else
	  {
	    if (pcity->city_map[x][y] == C_TILE_UNAVAILABLE)
	    {
	      LONG x1 = _mleft(o) + x * get_normal_tile_width();
	      LONG y1 = _mtop(o) + y * get_normal_tile_height();
	      LONG x2 = x1 + get_normal_tile_width() - 2;
	      LONG y2 = y1 + get_normal_tile_height() - 2;

	      SetAPen(rp, data->red_color);
	      Move(rp, x1, y1);
	      Draw(rp, x2, y1);
	      Draw(rp, x2, y2);
	      Draw(rp, x1, y2);
	      Draw(rp, x1, y1 + 1);
	    }
	  }
	}
	else
	{
/*
	  pixmap_put_black_tile(pdialog->map_canvas->window, x, y);
*/
	}
      }
    }
  }
  return 0;
}

STATIC ULONG CityMap_HandleInput(struct IClass * cl, Object * o, struct MUIP_HandleInput * msg)
{
  struct CityMap_Data *data = (struct CityMap_Data *) INST_DATA(cl, o);

  if (msg->imsg)
  {
    switch (msg->imsg->Class)
    {
    case IDCMP_MOUSEBUTTONS:
      if (msg->imsg->Code == SELECTDOWN)
      {
	if (_isinobject(msg->imsg->MouseX, msg->imsg->MouseY))
	{
	  LONG x = msg->imsg->MouseX - _mleft(o);
	  LONG y = msg->imsg->MouseY - _mtop(o);
	  x /= get_normal_tile_width();
	  y /= get_normal_tile_height();

	  data->click.x = map_adjust_x(x);
	  data->click.y = map_adjust_y(y);

	  set(o, MUIA_CityMap_Click, &data->click);
	}
      }
      break;
    }
  }
  return 0;
}

STATIC ULONG CityMap_Refresh(struct IClass * cl, Object * o, Msg msg)
{
  struct CityMap_Data *data = (struct CityMap_Data *) INST_DATA(cl, o);
  MUI_Redraw(o, MADF_DRAWUPDATE);
  return 0;
}

STATIC __asm __saveds ULONG CityMap_Dispatcher(register __a0 struct IClass * cl, register __a2 Object * obj, register __a1 Msg msg)
{
  switch (msg->MethodID)
  {
  case OM_NEW:
    return CityMap_New(cl, obj, (struct opSet *) msg);
  case OM_DISPOSE:
    return CityMap_Dispose(cl, obj, msg);
  case OM_GET:
    return CityMap_Get(cl, obj, (struct opGet *) msg);
  case OM_SET:
    return CityMap_Set(cl, obj, (struct opSet *) msg);
  case MUIM_Setup:
    return CityMap_Setup(cl, obj, msg);
  case MUIM_Cleanup:
    return CityMap_Cleanup(cl, obj, msg);
  case MUIM_AskMinMax:
    return CityMap_AskMinMax(cl, obj, (struct MUIP_AskMinMax *) msg);
  case MUIM_Show:
    return CityMap_Show(cl, obj, msg);
  case MUIM_Draw:
    return CityMap_Draw(cl, obj, (struct MUIP_Draw *) msg);
  case MUIM_HandleInput:
    return CityMap_HandleInput(cl, obj, (struct MUIP_HandleInput *) msg);
  case MUIM_CityMap_Refresh:
    return CityMap_Refresh(cl, obj, msg);
  }
  return (DoSuperMethodA(cl, obj, msg));
}

/****************************************************************
 SpaceShip Custom Class
*****************************************************************/

struct MUI_CustomClass *CL_SpaceShip;

Object *MakeSpaceShip(struct player_spaceship *ship)
{
  return SpaceShipObject,
    MUIA_SpaceShip_Ship, ship,
    End;
}

struct SpaceShip_Data
{
  struct player_spaceship *ship;
};


STATIC ULONG SpaceShip_New(struct IClass *cl, Object * o, struct opSet *msg)
{
  if ((o = (Object *) DoSuperMethodA(cl, o, (Msg) msg)))
  {
    struct SpaceShip_Data *data = (struct SpaceShip_Data *) INST_DATA(cl, o);
    struct TagItem *tl = msg->ops_AttrList;
    struct TagItem *ti;

    while ((ti = NextTagItem(&tl)))
    {
      switch (ti->ti_Tag)
      {
      case MUIA_SpaceShip_Ship:
	data->ship = (struct player_spaceship *) ti->ti_Data;
	break;
      }
    }
  }
  return (ULONG) o;
}

STATIC ULONG SpaceShip_AskMinMax(struct IClass * cl, Object * o, struct MUIP_AskMinMax * msg)
{
  LONG width, height;
  DoSuperMethodA(cl, o, (Msg) msg);

  width = sprites.spaceship.habitation->width * 7;
  height = sprites.spaceship.habitation->height * 7;

  msg->MinMaxInfo->MinWidth += width;
  msg->MinMaxInfo->DefWidth += width;
  msg->MinMaxInfo->MaxWidth += width;

  msg->MinMaxInfo->MinHeight += height;
  msg->MinMaxInfo->DefHeight += height;
  msg->MinMaxInfo->MaxHeight += height;
  return 0;
}

STATIC ULONG SpaceShip_Draw(struct IClass * cl, Object * o, struct MUIP_Draw * msg)
{
  struct SpaceShip_Data *data = (struct SpaceShip_Data *) INST_DATA(cl, o);
  struct RastPort *rp = _rp(o);

  DoSuperMethodA(cl, o, (Msg) msg);

  {
    int i, j, k, x, y;
    struct Sprite *sprite = sprites.spaceship.habitation;	/* for size */
    struct player_spaceship *ship = data->ship;

    SetAPen(rp, 1);		/* black */
    RectFill(rp, _mleft(o), _mtop(o), _mright(o), _mbottom(o));

    for (i = 0; i < NUM_SS_MODULES; i++)
    {
      j = i / 3;
      k = i % 3;
      if ((k == 0 && j >= ship->habitation) || (k == 1 && j >= ship->life_support) || (k == 2 && j >= ship->solar_panels))
	continue;

      x = modules_info[i].x * sprite->width / 4 - sprite->width / 2;
      y = modules_info[i].y * sprite->height / 4 - sprite->height / 2;

      sprite = (k == 0 ? sprites.spaceship.habitation :
		k == 1 ? sprites.spaceship.life_support :
		sprites.spaceship.solar_panels);

      put_sprite_overlay(rp, sprite, _mleft(o) + x, _mtop(o) + y);
    }

    for (i = 0; i < NUM_SS_COMPONENTS; i++)
    {
      j = i / 2;
      k = i % 2;
      if ((k == 0 && j >= ship->fuel) || (k == 1 && j >= ship->propulsion))
	continue;

      x = components_info[i].x * sprite->width / 4 - sprite->width / 2;
      y = components_info[i].y * sprite->height / 4 - sprite->height / 2;

      sprite = (k == 0) ? sprites.spaceship.fuel : sprites.spaceship.propulsion;

      put_sprite_overlay(rp, sprite, _mleft(o) + x, _mtop(o) + y);
    }

    sprite = sprites.spaceship.structural;

    for (i = 0; i < NUM_SS_STRUCTURALS; i++)
    {
      if (!ship->structure[i])
	continue;
      x = structurals_info[i].x * sprite->width / 4 - sprite->width / 2;
      y = structurals_info[i].y * sprite->height / 4 - sprite->height / 2;

      put_sprite_overlay(rp, sprite, _mleft(o) + x, _mtop(o) + y);
    }
  }
  return 0;
}

STATIC __asm __saveds ULONG SpaceShip_Dispatcher(register __a0 struct IClass * cl, register __a2 Object * obj, register __a1 Msg msg)
{
  switch (msg->MethodID)
  {
  case OM_NEW:
    return SpaceShip_New(cl, obj, (struct opSet *) msg);
  case MUIM_AskMinMax:
    return SpaceShip_AskMinMax(cl, obj, (struct MUIP_AskMinMax *) msg);
  case MUIM_Draw:
    return SpaceShip_Draw(cl, obj, (struct MUIP_Draw *) msg);
  }
  return (DoSuperMethodA(cl, obj, msg));
}


/****************************************************************
 Sprite Custom Class
*****************************************************************/

struct MUI_CustomClass *CL_Sprite;

Object *MakeSprite(struct Sprite *sprite)
{
  return SpriteObject,
    MUIA_Sprite_Sprite, sprite,
    MUIA_InputMode, MUIV_InputMode_RelVerify,
    MUIA_ShowSelState, FALSE,
    End;
}

Object *MakeBorderSprite(struct Sprite *sprite)
{
  return SpriteObject,
    TextFrame,
    InnerSpacing(0,0),
    MUIA_Sprite_Sprite, sprite,
    End;
}

struct Sprite_Data
{
  struct Sprite *sprite;
};


STATIC ULONG Sprite_New(struct IClass *cl, Object * o, struct opSet *msg)
{
  if ((o = (Object *) DoSuperMethodA(cl, o, (Msg) msg)))
  {
    struct Sprite_Data *data = (struct Sprite_Data *) INST_DATA(cl, o);
    struct TagItem *tl = msg->ops_AttrList;
    struct TagItem *ti;

    while ((ti = NextTagItem(&tl)))
    {
      switch (ti->ti_Tag)
      {
      case MUIA_Sprite_Sprite:
	data->sprite = (struct Sprite *) ti->ti_Data;
	break;
      }
    }
  }
  return (ULONG) o;
}

STATIC ULONG Sprite_Dispose(struct IClass * cl, Object * o, Msg msg)
{
  struct Sprite_Data *data = (struct Sprite_Data *) INST_DATA(cl, o);
  return DoSuperMethodA(cl, o, msg);
}

STATIC ULONG Sprite_Set(struct IClass * cl, Object * o, struct opSet * msg)
{
  struct Sprite_Data *data = (struct Sprite_Data *) INST_DATA(cl, o);
  struct TagItem *tl = msg->ops_AttrList;
  struct TagItem *ti;

  while ((ti = NextTagItem(&tl)))
  {
    switch (ti->ti_Tag)
    {
    case MUIA_Sprite_Sprite:
      data->sprite = (struct Sprite *) ti->ti_Data;
      MUI_Redraw(o, MADF_DRAWUPDATE);
      break;
    }
  }

  return DoSuperMethodA(cl, o, (Msg) msg);
}

STATIC ULONG Sprite_Setup(struct IClass * cl, Object * o, Msg msg)
{
  struct Sprite_Data *data = (struct Sprite_Data *) INST_DATA(cl, o);
  if (!DoSuperMethodA(cl, o, msg))
    return FALSE;

  MUI_RequestIDCMP(o, IDCMP_MOUSEBUTTONS);

  return TRUE;

}

STATIC ULONG Sprite_Cleanup(struct IClass * cl, Object * o, Msg msg)
{
  struct Sprite_Data *data = (struct Sprite_Data *) INST_DATA(cl, o);

  MUI_RejectIDCMP(o, IDCMP_MOUSEBUTTONS);

  DoSuperMethodA(cl, o, msg);
  return 0;
}

STATIC ULONG Sprite_AskMinMax(struct IClass * cl, Object * o, struct MUIP_AskMinMax * msg)
{
  struct Sprite_Data *data = (struct Sprite_Data *) INST_DATA(cl, o);
  DoSuperMethodA(cl, o, (Msg) msg);

  if (data->sprite)
  {
    msg->MinMaxInfo->MinWidth += data->sprite->width;
    msg->MinMaxInfo->DefWidth += data->sprite->width;
    msg->MinMaxInfo->MaxWidth += data->sprite->width;

    msg->MinMaxInfo->MinHeight += data->sprite->height;
    msg->MinMaxInfo->DefHeight += data->sprite->height;
    msg->MinMaxInfo->MaxHeight += data->sprite->height;
  }
  else
  {
    msg->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    msg->MinMaxInfo->MaxHeight += MUI_MAXMAX;
  }
  return 0;
}

STATIC ULONG Sprite_Draw(struct IClass * cl, Object * o, struct MUIP_Draw * msg)
{
  struct Sprite_Data *data = (struct Sprite_Data *) INST_DATA(cl, o);
  struct RastPort *rp = _rp(o);
  APTR cliphandle;

  DoSuperMethodA(cl, o, (Msg) msg);

  put_sprite(_rp(o), data->sprite, _mleft(o), _mtop(o));

  return 0;
}

STATIC __asm __saveds ULONG Sprite_Dispatcher(register __a0 struct IClass * cl, register __a2 Object * obj, register __a1 Msg msg)
{
  switch (msg->MethodID)
  {
  case OM_NEW:
    return Sprite_New(cl, obj, (struct opSet *) msg);
  case OM_DISPOSE:
    return Sprite_Dispose(cl, obj, msg);
  case OM_SET:
    return Sprite_Set(cl, obj, (struct opSet *) msg);
  case MUIM_Setup:
    return Sprite_Setup(cl, obj, msg);
  case MUIM_Cleanup:
    return Sprite_Cleanup(cl, obj, msg);
  case MUIM_AskMinMax:
    return Sprite_AskMinMax(cl, obj, (struct MUIP_AskMinMax *) msg);
  case MUIM_Draw:
    return Sprite_Draw(cl, obj, (struct MUIP_Draw *) msg);
  }
  return (DoSuperMethodA(cl, obj, msg));
}

/****************************************************************
 Unit Custom Class
*****************************************************************/

struct MUI_CustomClass *CL_Unit;

Object *MakeUnit(struct unit *punit, LONG upkeep)
{
  return UnitObject,
    MUIA_Unit_Unit, punit,
    MUIA_Unit_Upkeep, upkeep,
    MUIA_InputMode, MUIV_InputMode_RelVerify,
    End;
}

struct Unit_Data
{
  struct unit *punit;
  enum unit_activity activity;
  BOOL upkeep;
};


STATIC ULONG Unit_New(struct IClass *cl, Object * o, struct opSet *msg)
{
  if ((o = (Object *) DoSuperMethodA(cl, o, (Msg) msg)))
  {
    struct Unit_Data *data = (struct Unit_Data *) INST_DATA(cl, o);
    struct TagItem *tl = msg->ops_AttrList;
    struct TagItem *ti;

    while ((ti = NextTagItem(&tl)))
    {
      switch (ti->ti_Tag)
      {
      case MUIA_Unit_Unit:
	data->punit = (struct unit *) ti->ti_Data;
	break;

      case MUIA_Unit_Upkeep:
	data->upkeep = ti->ti_Data ? TRUE : FALSE;
	break;
      }
    }
  }
  return (ULONG) o;
}

STATIC ULONG Unit_Dispose(struct IClass * cl, Object * o, Msg msg)
{
  return DoSuperMethodA(cl, o, msg);
}

STATIC ULONG Unit_Set(struct IClass * cl, Object * o, struct opSet * msg)
{
  struct Unit_Data *data = (struct Unit_Data *) INST_DATA(cl, o);
  struct TagItem *tl = msg->ops_AttrList;
  struct TagItem *ti;

  while ((ti = NextTagItem(&tl)))
  {
    switch (ti->ti_Tag)
    {
    case MUIA_Unit_Unit:
      {
	struct unit *punit = (struct unit *) ti->ti_Data;
	if (data->punit == punit)
	{
	  if (punit)
	  {
	    if (data->activity == punit->activity)
	      break;
	  }
	}

	data->punit = punit;
	data->activity = punit ? punit->activity : 0;
	MUI_Redraw(o, MADF_DRAWOBJECT);
      }
      break;
    }
  }

  return DoSuperMethodA(cl, o, (Msg) msg);
}

STATIC ULONG Unit_Setup(struct IClass * cl, Object * o, Msg msg)
{
  if (!DoSuperMethodA(cl, o, msg))
    return FALSE;

  MUI_RequestIDCMP(o, IDCMP_MOUSEBUTTONS);

  return TRUE;

}

STATIC ULONG Unit_Cleanup(struct IClass * cl, Object * o, Msg msg)
{
  MUI_RejectIDCMP(o, IDCMP_MOUSEBUTTONS);

  DoSuperMethodA(cl, o, msg);
  return 0;
}

STATIC ULONG Unit_AskMinMax(struct IClass * cl, Object * o, struct MUIP_AskMinMax * msg)
{
  struct Unit_Data *data = (struct Unit_Data *) INST_DATA(cl, o);
  LONG w = get_normal_tile_width();
  LONG h = get_normal_tile_height();
  DoSuperMethodA(cl, o, (Msg) msg);

  if (data->upkeep)
    h += get_normal_tile_height() / 2;

  msg->MinMaxInfo->MinWidth += w;
  msg->MinMaxInfo->DefWidth += w;
  msg->MinMaxInfo->MaxWidth += w;

  msg->MinMaxInfo->MinHeight += h;
  msg->MinMaxInfo->DefHeight += h;
  msg->MinMaxInfo->MaxHeight += h;
  return 0;
}

STATIC ULONG Unit_Draw(struct IClass * cl, Object * o, struct MUIP_Draw * msg)
{
  struct Unit_Data *data = (struct Unit_Data *) INST_DATA(cl, o);
  struct unit *punit = data->punit;

  DoSuperMethodA(cl, o, (Msg) msg);

  if (punit)
  {
    put_unit_tile(_rp(o), punit, _mleft(o), _mtop(o));
    if (data->upkeep)
    {
      LONG y = _mtop(o) + get_normal_tile_height();

      /* draw overlay pixmaps */
      /* FIXME:
       * For now only two food, one shield and two masks can be drawn per unit,
       * the proper way to do this is probably something like what Civ II does.
       * (One food/shield/mask drawn N times, possibly one top of itself. -- SKi */

      {				// from put_unit_gpixmap_city_overlay

	int upkeep_food = CLIP(0, punit->upkeep_food, 2);
	int unhappy = CLIP(0, punit->unhappiness, 2);
	int x = _mleft(o);

	/* draw overlay pixmaps */
	if (punit->upkeep > 0)
	  put_sprite_overlay_height(_rp(o), sprites.upkeep.shield, x, y, get_normal_tile_height() / 2);
	if (upkeep_food > 0)
	  put_sprite_overlay_height(_rp(o), sprites.upkeep.food[upkeep_food - 1], x, y, get_normal_tile_height() / 2);
	if (unhappy > 0)
	  put_sprite_overlay_height(_rp(o), sprites.upkeep.unhappy[unhappy - 1], x, y, get_normal_tile_height() / 2);
      }
    }
  }
  return 0;
}

STATIC __asm __saveds ULONG Unit_Dispatcher(register __a0 struct IClass * cl, register __a2 Object * obj, register __a1 Msg msg)
{
  switch (msg->MethodID)
  {
  case OM_NEW:
    return Unit_New(cl, obj, (struct opSet *) msg);
  case OM_DISPOSE:
    return Unit_Dispose(cl, obj, msg);
  case OM_SET:
    return Unit_Set(cl, obj, (struct opSet *) msg);
  case MUIM_Setup:
    return Unit_Setup(cl, obj, msg);
  case MUIM_Cleanup:
    return Unit_Cleanup(cl, obj, msg);
  case MUIM_AskMinMax:
    return Unit_AskMinMax(cl, obj, (struct MUIP_AskMinMax *) msg);
  case MUIM_Draw:
    return Unit_Draw(cl, obj, (struct MUIP_Draw *) msg);
  }
  return (DoSuperMethodA(cl, obj, msg));
}

/****************************************************************
 MyGauge Custom Class
*****************************************************************/

struct MUI_CustomClass *CL_MyGauge;

struct MyGauge_Data
{
  STRPTR info_text;
};

STATIC ULONG MyGauge_Dispose(struct IClass *cl, Object * o, Msg msg)
{
  struct MyGauge_Data *data = (struct MyGauge_Data *) INST_DATA(cl, o);
  if (data->info_text)
    FreeVec(data->info_text);
  return DoSuperMethodA(cl, o, msg);
}

STATIC ULONG MyGauge_SetGauge(struct IClass * cl, Object * o, struct MUIP_MyGauge_SetGauge * msg)
{
  struct MyGauge_Data *data = (struct MyGauge_Data *) INST_DATA(cl, o);
  if (data->info_text)
    FreeVec(data->info_text);
  data->info_text = StrCopy(msg->info);

  SetAttrs(o,
	   MUIA_Gauge_Max, msg->max,
	   MUIA_Gauge_Current, msg->current,
	   MUIA_Gauge_InfoText, data->info_text,
	   TAG_DONE);

  return 0;
}

STATIC __asm __saveds ULONG MyGauge_Dispatcher(register __a0 struct IClass * cl, register __a2 Object * obj, register __a1 Msg msg)
{
  switch (msg->MethodID)
  {
  case OM_DISPOSE:
    return MyGauge_Dispose(cl, obj, msg);
  case MUIM_MyGauge_SetGauge:
    return MyGauge_SetGauge(cl, obj, (struct MUIP_MyGauge_SetGauge *) msg);
  }
  return (DoSuperMethodA(cl, obj, msg));
}

/****************************************************************
 Initialize all custom classes
*****************************************************************/
BOOL create_map_class(void)
{
  if ((CL_TilePopWindow = MUI_CreateCustomClass(NULL, MUIC_Window, NULL, 4, TilePopWindow_Dispatcher)))
    if ((CL_Map = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct Map_Data), Map_Dispatcher)))
      if ((CL_CityMap = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct CityMap_Data), CityMap_Dispatcher)))
	if ((CL_SpaceShip = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct SpaceShip_Data), SpaceShip_Dispatcher)))
	  if ((CL_Sprite = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct Sprite_Data), Sprite_Dispatcher)))
	    if ((CL_Unit = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct Unit_Data), Unit_Dispatcher)))
	      if ((CL_MyGauge = MUI_CreateCustomClass(NULL, MUIC_Gauge, NULL, sizeof(struct MyGauge_Data), MyGauge_Dispatcher)))
		  return TRUE;
  return FALSE;
}

/****************************************************************
 Remove all custom classes
*****************************************************************/
VOID delete_map_class(void)
{
  if (CL_MyGauge)
    MUI_DeleteCustomClass(CL_MyGauge);
  if (CL_Unit)
    MUI_DeleteCustomClass(CL_Unit);
  if (CL_Sprite)
    MUI_DeleteCustomClass(CL_Sprite);
  if (CL_SpaceShip)
    MUI_DeleteCustomClass(CL_SpaceShip);
  if (CL_CityMap)
    MUI_DeleteCustomClass(CL_CityMap);
  if (CL_Map)
    MUI_DeleteCustomClass(CL_Map);
  if (CL_TilePopWindow)
    MUI_DeleteCustomClass(CL_TilePopWindow);
}
