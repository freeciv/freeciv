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

struct drawn_sprite {
  enum {
    DRAWN_SPRITE,	/* Draw a sprite. */
    DRAWN_GRID,		/* Draw the map grid now. */
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
      struct tile *tile;
      bool citymode;
    } grid;

    struct {
      enum color_std color;
    } bg;
  } data;
};

const char **get_tileset_list(void);

bool tilespec_read_toplevel(const char *tileset_name);
void tilespec_load_tiles(void);
void tilespec_free_tiles(void);

void tilespec_reread(const char *tileset_name);
void tilespec_reread_callback(struct client_option *option);

void tilespec_setup_specialist_types(void);
void tilespec_setup_unit_type(int id);
void tilespec_setup_impr_type(int id);
void tilespec_setup_tech_type(int id);
void tilespec_setup_tile_type(Terrain_type_id terrain);
void tilespec_setup_government(int id);
void tilespec_setup_nation_flag(int id);
void tilespec_setup_city_tiles(int style);
void tilespec_alloc_city_tiles(int count);
void tilespec_free_city_tiles(int count);

/* Gfx support */

int fill_sprite_array(struct drawn_sprite *sprs, struct tile *ptile,
		      struct unit *punit, struct city *pcity,
		      bool citymode);

enum color_std player_color(struct player *pplayer);
enum color_std overview_tile_color(struct tile *ptile);

void set_focus_unit_hidden_state(bool hide);
struct unit *get_drawable_unit(struct tile *ptile, bool citymode);


/* This the way directional indices are now encoded: */
#define MAX_INDEX_CARDINAL 		64
#define MAX_INDEX_HALF                  16
#define MAX_INDEX_VALID			256

#define NUM_TILES_PROGRESS 8
#define NUM_TILES_CITIZEN CITIZEN_LAST
#define NUM_TILES_HP_BAR 11
#define NUM_TILES_DIGITS 10
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

struct named_sprites {
  struct Sprite
    *bulb[NUM_TILES_PROGRESS],
    *warming[NUM_TILES_PROGRESS],
    *cooling[NUM_TILES_PROGRESS],
    *treaty_thumb[2],     /* 0=disagree, 1=agree */
    *right_arrow,

    /* The panel sprites for showing tax % allocations. */
    *tax_luxury, *tax_science, *tax_gold,

    *black_tile,      /* only used for isometric view */
    *dither_tile;     /* only used for isometric view */

  struct citizen_graphic {
    /* Each citizen type has up to MAX_NUM_CITIZEN_SPRITES different
     * sprites, as defined by the tileset. */
    int count;
    struct Sprite *sprite[MAX_NUM_CITIZEN_SPRITES];
  } citizen[NUM_TILES_CITIZEN], specialist[SP_COUNT];
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
    struct Sprite **unit;
    struct Sprite *nuke;
  } explode;
  struct {
    struct Sprite
      *hp_bar[NUM_TILES_HP_BAR],
      *vet_lev[MAX_VET_LEVELS],
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
      *food[2],
      *unhappy[2],
      *gold[2],
      *shield;
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
      *spec_river[MAX_INDEX_CARDINAL],
      *darkness[MAX_INDEX_CARDINAL],         /* first unused */
      *river_outlet[4];		/* indexed by enum direction4 */
  } tx;				/* terrain extra */

  struct terrain_drawing_data *terrain[MAX_NUM_TERRAINS];
};

extern struct named_sprites sprites;
extern int fogstyle;

struct Sprite *get_citizen_sprite(struct citizen_type type,
				  int citizen_index,
				  const struct city *pcity);

/* full pathnames: */
extern char *main_intro_filename;
extern char *minimap_intro_filename;

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

extern int NORMAL_TILE_WIDTH;
extern int NORMAL_TILE_HEIGHT;
extern int UNIT_TILE_WIDTH;
extern int UNIT_TILE_HEIGHT;
extern int SMALL_TILE_WIDTH;
extern int SMALL_TILE_HEIGHT;

/* The overview tile width and height are defined in terms of the base
 * size.  For iso-maps the width is twice the height since "natural"
 * coordinates are used.  For classical maps the width and height are
 * equal.  The base size may be adjusted to get the correct scale. */
extern int OVERVIEW_TILE_SIZE;
#define OVERVIEW_TILE_WIDTH ((MAP_IS_ISOMETRIC ? 2 : 1) * OVERVIEW_TILE_SIZE)
#define OVERVIEW_TILE_HEIGHT OVERVIEW_TILE_SIZE

extern bool is_isometric;
extern int hex_width, hex_height;

/* name of font to use to draw city names on main map */

extern char *city_names_font;

/* name of font to use to draw city productions on main map */

extern char *city_productions_font_name;

extern int num_tiles_explode_unit;

struct Sprite *load_sprite(const char *tag_name);
void unload_sprite(const char *tag_name);
bool sprite_exists(const char *tag_name);
void finish_loading_sprites(void);

#endif  /* FC__TILESPEC_H */
