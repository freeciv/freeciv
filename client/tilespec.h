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

/********************************************************************** 
  Reading and using the tilespec files, which describe
  the files and contents of tilesets.
***********************************************************************/
#ifndef FC__TILESPEC_H
#define FC__TILESPEC_H

#include "fc_types.h"

#include "citydlg_common.h"	/* enum citizen_type */
#include "colors_g.h"
#include "options.h"

struct Sprite;			/* opaque; gui-dep */

/* An edge is the border between two tiles.  This structure represents one
 * edge.  The tiles are given in the same order as the enumeration name. */
struct tile_edge {
  enum {
    EDGE_NS, /* North and south */
    EDGE_WE, /* West and east */
    EDGE_UD, /* Up and down (nw/se), for hex_width tilesets */
    EDGE_LR, /* Left and right (ne/sw), for hex_height tilesets */
    EDGE_COUNT
  } type;
#define NUM_EDGE_TILES 2
  const struct tile *tile[NUM_EDGE_TILES];
};

/* A corner is the endpoint of several edges.  At each corner 4 tiles will
 * meet (3 in hex view).  Tiles are in clockwise order NESW. */
struct tile_corner {
#define NUM_CORNER_TILES 4
  const struct tile *tile[NUM_CORNER_TILES];
};

struct drawn_sprite {
  enum {
    DRAWN_SPRITE,	/* Draw a sprite. */
    DRAWN_BG            /* Draw a solid BG. */
  } type;

  union {
    struct {
      enum {
	/* Only applicable in iso-view.  "Full" sprites overlap into the top
	 * half-tile of UNIT_TILE_HEIGHT. */
	DRAW_NORMAL,
	DRAW_FULL
      } style;
      bool foggable;	/* Set to FALSE for sprites that are never fogged. */
      struct Sprite *sprite;
      int offset_x, offset_y;	/* offset from tile origin */
    } sprite;

    struct {
      enum color_std color;
    } bg;
  } data;
};

/* Items on the mapview are drawn in layers.  Each entry below represents
 * one layer.  The names are basically arbitrary and just correspond to
 * groups of elements in fill_sprite_array().  Callers of fill_sprite_array
 * must call it once for each layer. */
enum mapview_layer {
  LAYER_BACKGROUND,
  LAYER_TERRAIN1,
  LAYER_TERRAIN2,
  LAYER_WATER,
  LAYER_ROADS,
  LAYER_SPECIAL1,
  LAYER_GRID1,
  LAYER_CITY1,
  LAYER_SPECIAL2,
  LAYER_FOG,
  LAYER_CITY2,
  LAYER_UNIT,
  LAYER_SPECIAL3,
  LAYER_GRID2,
  LAYER_OVERLAYS,
  LAYER_GOTO,
  LAYER_COUNT
};

#define mapview_layer_iterate(layer)			                    \
{									    \
  enum mapview_layer layer;						    \
									    \
  for (layer = 0; layer < LAYER_COUNT; layer++) {			    \

#define mapview_layer_iterate_end		                            \
  }									    \
}

struct tileset;

extern struct tileset *tileset;

const char **get_tileset_list(void);

struct tileset *tileset_read_toplevel(const char *tileset_name);
void tileset_free(struct tileset *tileset);
void tileset_load_tiles(struct tileset *t);
void tileset_free_tiles(struct tileset *t);

void tilespec_reread(const char *tileset_name);
void tilespec_reread_callback(struct client_option *option);

void tileset_setup_specialist_types(struct tileset *t);
void tileset_setup_unit_type(struct tileset *t, int id);
void tileset_setup_impr_type(struct tileset *t, int id);
void tileset_setup_tech_type(struct tileset *t, int id);
void tileset_setup_tile_type(struct tileset *t, Terrain_type_id terrain);
void tileset_setup_government(struct tileset *t, int id);
void tileset_setup_nation_flag(struct tileset *t, int id);
void tileset_setup_city_tiles(struct tileset *t, int style);
void tileset_alloc_city_tiles(struct tileset *t, int count);
void tileset_free_city_tiles(struct tileset *t, int count);

/* Gfx support */

int fill_sprite_array(struct tileset *t,
		      struct drawn_sprite *sprs, enum mapview_layer layer,
		      const struct tile *ptile,
		      const struct tile_edge *pedge,
		      const struct tile_corner *pcorner,
		      const struct unit *punit, const struct city *pcity,
		      const struct city *citymode);

enum color_std player_color(const struct player *pplayer);
enum color_std overview_tile_color(struct tile *ptile);

double get_focus_unit_toggle_timeout(void);
void reset_focus_unit_state(void);
void toggle_focus_unit_state(void);
struct unit *get_drawable_unit(struct tile *ptile,
			       const struct city *citymode);


/* This the way directional indices are now encoded: */
#define MAX_INDEX_CARDINAL 		64
#define MAX_INDEX_HALF                  16
#define MAX_INDEX_VALID			256

#define NUM_TILES_PROGRESS 8
#define NUM_TILES_CITIZEN CITIZEN_LAST
#define NUM_TILES_HP_BAR 11
#define NUM_TILES_DIGITS 10
#define NUM_TILES_SELECT 4
#define MAX_NUM_CITIZEN_SPRITES 6

/* This could be moved to common/map.h if there's more use for it. */
enum direction4 {
  DIR4_NORTH = 0, DIR4_SOUTH, DIR4_EAST, DIR4_WEST
};

enum match_style {
  MATCH_NONE, MATCH_BOOLEAN, MATCH_FULL
};

enum cell_type {
  CELL_SINGLE, CELL_RECT
};

#define MAX_NUM_LAYERS 2

/* Create the sprite_vector type. */
#define SPECVEC_TAG sprite
#define SPECVEC_TYPE struct Sprite *
#include "specvec.h"

struct terrain_drawing_data {
  char *name;
  char *mine_tag;

  int num_layers; /* Can only be 1 or 2. */
  struct {
    bool is_tall;
    int offset_x, offset_y;

    enum match_style match_style;
    int match_type, match_count;

    enum cell_type cell_type;

    struct sprite_vector base;
    struct Sprite *match[MAX_INDEX_CARDINAL];
    struct Sprite **cells;
  } layer[MAX_NUM_LAYERS];

  bool is_blended;
  struct Sprite *blend[4]; /* indexed by a direction4 */

  struct Sprite *special[2];
  struct Sprite *mine;
};

enum cursor_type {
  CURSOR_GOTO,
  CURSOR_PATROL,
  CURSOR_PARADROP,
  CURSOR_NUKE,
  CURSOR_LAST
};

enum indicator_type {
  INDICATOR_BULB,
  INDICATOR_WARMING,
  INDICATOR_COOLING,
  INDICATOR_COUNT
};

struct named_sprites {
  struct Sprite
    *indicator[INDICATOR_COUNT][NUM_TILES_PROGRESS],
    *treaty_thumb[2],     /* 0=disagree, 1=agree */
    *right_arrow,

    /* The panel sprites for showing tax % allocations. */
    *tax_luxury, *tax_science, *tax_gold,
    *dither_tile;     /* only used for isometric view */

  struct {
    struct Sprite
      *tile,
      *worked_tile,
      *unworked_tile;
  } mask;

  struct citizen_graphic {
    /* Each citizen type has up to MAX_NUM_CITIZEN_SPRITES different
     * sprites, as defined by the tileset. */
    int count;
    struct Sprite *sprite[MAX_NUM_CITIZEN_SPRITES];
  } citizen[NUM_TILES_CITIZEN], specialist[SP_MAX];
  struct {
    struct Sprite
      *solar_panels,
      *life_support,
      *habitation,
      *structural,
      *fuel,
      *propulsion;
  } spaceship;
  struct {
    int hot_x, hot_y;
    struct Sprite *icon;
  } cursor[CURSOR_LAST];
  struct {
    struct Sprite
      /* for roadstyle 0 */
      *dir[8],     /* all entries used */
      /* for roadstyle 1 */
      *even[MAX_INDEX_HALF],    /* first unused */
      *odd[MAX_INDEX_HALF],     /* first unused */
      /* for roadstyle 0 and 1 */
      *isolated,
      *corner[8], /* Indexed by direction; only non-cardinal dirs used. */
      *total[MAX_INDEX_VALID];     /* includes all possibilities */
  } road, rail;
  struct {
    struct sprite_vector unit;
    struct Sprite *nuke;
  } explode;
  struct {
    struct Sprite
      *hp_bar[NUM_TILES_HP_BAR],
      *vet_lev[MAX_VET_LEVELS],
      *select[NUM_TILES_SELECT],
      *auto_attack,
      *auto_settler,
      *auto_explore,
      *fallout,
      *fortified,
      *fortifying,
      *fortress,
      *airbase,
      *go_to,			/* goto is a C keyword :-) */
      *irrigate,
      *mine,
      *pillage,
      *pollution,
      *road,
      *sentry,
      *stack,
      *loaded,
      *transform,
      *connect,
      *patrol,
      *lowfuel,
      *tired;
  } unit;
  struct {
    struct Sprite
      *unhappy[2],
      *output[O_MAX][2];
  } upkeep;
  struct {
    struct Sprite
      *occupied,
      *disorder,
      *size[NUM_TILES_DIGITS],
      *size_tens[NUM_TILES_DIGITS],		/* first unused */
      *tile_foodnum[NUM_TILES_DIGITS],
      *tile_shieldnum[NUM_TILES_DIGITS],
      *tile_tradenum[NUM_TILES_DIGITS],
      ***tile_wall,      /* only used for isometric view */
      ***tile;
    struct sprite_vector worked_tile_overlay;
    struct sprite_vector unworked_tile_overlay;
  } city;
  struct {
    struct Sprite
      *turns[NUM_TILES_DIGITS],
      *turns_tens[NUM_TILES_DIGITS];
  } path;
  struct {
    struct Sprite *attention;
  } user;
  struct {
    struct Sprite
      *farmland[MAX_INDEX_CARDINAL],
      *irrigation[MAX_INDEX_CARDINAL],
      *pollution,
      *village,
      *fortress,
      *fortress_back,
      *airbase,
      *fallout,
      *fog,
      **fullfog,
      *spec_river[MAX_INDEX_CARDINAL],
      *darkness[MAX_INDEX_CARDINAL],         /* first unused */
      *river_outlet[4];		/* indexed by enum direction4 */
  } tx;				/* terrain extra */
  struct {
    struct Sprite
      *main[EDGE_COUNT],
      *city[EDGE_COUNT],
      *worked[EDGE_COUNT],
      *unavailable,
      *selected[EDGE_COUNT],
      *coastline[EDGE_COUNT],
      *borders[EDGE_COUNT][2],
      *player_borders[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS][EDGE_COUNT][2];
  } grid;
  struct {
    struct sprite_vector overlays;

    struct Sprite *player[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];
  } colors;

  struct terrain_drawing_data *terrain[MAX_NUM_TERRAINS];
};

extern struct named_sprites sprites;

struct Sprite *get_citizen_sprite(struct citizen_type type,
				  int citizen_index,
				  const struct city *pcity);
struct Sprite *get_sample_city_sprite(int city_style);
struct Sprite *get_arrow_sprite(void);
struct Sprite *get_tax_sprite(Output_type_id otype);
struct Sprite *get_treaty_thumb_sprite(bool on_off);
struct sprite_vector *get_unit_explode_animation(void);
struct Sprite *get_cursor_sprite(enum cursor_type cursor,
				 int *hot_x, int *hot_y);
struct Sprite *get_attention_crosshair_sprite(void);
struct Sprite *get_indicator_sprite(enum indicator_type indicator,
				    int index);

/* These variables contain the size of the tiles used within the game.
 *
 * "normal" tiles include most mapview graphics, particularly the basic
 * terrain graphics.
 *
 * "unit" tiles are those used for drawing units.  In iso view these are
 * larger than normal tiles to mimic a 3D effect.
 *
 * "small" tiles are used for extra "theme" graphics, particularly sprites
 * for citizens, governments, and other panel indicator icons.
 *
 * Various parts of the code may make additional assumptions, including:
 *   - in non-iso view:
 *     - NORMAL_TILE_WIDTH == NORMAL_TILE_HEIGHT
 *     - UNIT_TILE_WIDTH == NORMAL_TILE_WIDTH
 *     - UNIT_TILE_HEIGHT == NORMAL_TILE_HEIGHT
 *   - in iso-view:
 *     - NORMAL_TILE_WIDTH == 2 * NORMAL_TILE_HEIGHT
 *     - UNIT_TILE_WIDTH == NORMAL_TILE_WIDTH
 *     - UNIT_TILE_HEIGHT == NORMAL_TILE_HEIGHT * 3 / 2
 *     - NORMAL_TILE_WIDTH and NORMAL_TILE_HEIGHT are even
 */

#define NORMAL_TILE_WIDTH tileset_tile_width()
#define NORMAL_TILE_HEIGHT tileset_tile_height()
#define UNIT_TILE_WIDTH tileset_full_tile_width()
#define UNIT_TILE_HEIGHT tileset_full_tile_height()
#define SMALL_TILE_WIDTH tileset_small_sprite_width()
#define SMALL_TILE_HEIGHT tileset_small_sprite_height()

/* The overview tile width and height are defined in terms of the base
 * size.  For iso-maps the width is twice the height since "natural"
 * coordinates are used.  For classical maps the width and height are
 * equal.  The base size may be adjusted to get the correct scale. */
extern int OVERVIEW_TILE_SIZE;
#define OVERVIEW_TILE_WIDTH ((MAP_IS_ISOMETRIC ? 2 : 1) * OVERVIEW_TILE_SIZE)
#define OVERVIEW_TILE_HEIGHT OVERVIEW_TILE_SIZE

/* Tileset accessor functions. */
bool tileset_is_isometric(void);
int tileset_hex_width(void);
int tileset_hex_height(void);
int tileset_tile_width(void);
int tileset_tile_height(void);
int tileset_full_tile_width(void);
int tileset_full_tile_height(void);
int tileset_small_sprite_width(void);
int tileset_small_sprite_height(void);
const char *tileset_main_intro_filename(void);
const char *tileset_mini_intro_filename(void);

struct Sprite *load_sprite(struct tileset *t, const char *tag_name);
void unload_sprite(struct tileset *t, const char *tag_name);
bool sprite_exists(struct tileset *t, const char *tag_name);

#endif  /* FC__TILESPEC_H */
