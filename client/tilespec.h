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

/* Create the sprite_vector type. */
#define SPECVEC_TAG sprite
#define SPECVEC_TYPE struct Sprite *
#include "specvec.h"

/* An edge is the border between two tiles.  This structure represents one
 * edge.  The tiles are given in the same order as the enumeration name. */
enum edge_type {
  EDGE_NS, /* North and south */
  EDGE_WE, /* West and east */
  EDGE_UD, /* Up and down (nw/se), for hex_width tilesets */
  EDGE_LR, /* Left and right (ne/sw), for hex_height tilesets */
  EDGE_COUNT
};
struct tile_edge {
  enum edge_type type;
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
  bool foggable;	/* Set to FALSE for sprites that are never fogged. */
  struct Sprite *sprite;
  int offset_x, offset_y;	/* offset from tile origin */
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
  LAYER_FOCUS_UNIT,
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

#define NUM_TILES_PROGRESS 8

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

enum icon_type {
  ICON_FREECIV,
  ICON_CITYDLG,
  ICON_COUNT
};

enum spaceship_part {
  SPACESHIP_SOLAR_PANEL,
  SPACESHIP_LIFE_SUPPORT,
  SPACESHIP_HABITATION,
  SPACESHIP_STRUCTURAL,
  SPACESHIP_FUEL,
  SPACESHIP_PROPULSION,
  SPACESHIP_COUNT
};

struct Sprite *get_spaceship_sprite(struct tileset *t,
				    enum spaceship_part part);
struct Sprite *get_citizen_sprite(struct tileset *t,
				  struct citizen_type type,
				  int citizen_index,
				  const struct city *pcity);
struct Sprite *get_sample_city_sprite(struct tileset *t, int city_style);
struct Sprite *get_arrow_sprite(struct tileset *t);
struct Sprite *get_tax_sprite(struct tileset *t, Output_type_id otype);
struct Sprite *get_treaty_thumb_sprite(struct tileset *t, bool on_off);
struct sprite_vector *get_unit_explode_animation(struct tileset *t);
struct Sprite *get_nuke_explode_sprite(struct tileset *t);
struct Sprite *get_cursor_sprite(struct tileset *t, enum cursor_type cursor,
				 int *hot_x, int *hot_y);
struct Sprite *get_icon_sprite(struct tileset *t, enum icon_type icon);
struct Sprite *get_attention_crosshair_sprite(struct tileset *t);
struct Sprite *get_indicator_sprite(struct tileset *t,
				    enum indicator_type indicator,
				    int index);
struct Sprite *get_unit_unhappy_sprite(struct tileset *t,
				       const struct unit *punit);
struct Sprite *get_unit_upkeep_sprite(struct tileset *t,
				      Output_type_id otype,
				      const struct unit *punit);

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

#define NORMAL_TILE_WIDTH tileset_tile_width(tileset)
#define NORMAL_TILE_HEIGHT tileset_tile_height(tileset)
#define UNIT_TILE_WIDTH tileset_full_tile_width(tileset)
#define UNIT_TILE_HEIGHT tileset_full_tile_height(tileset)
#define SMALL_TILE_WIDTH tileset_small_sprite_width(tileset)
#define SMALL_TILE_HEIGHT tileset_small_sprite_height(tileset)

/* The overview tile width and height are defined in terms of the base
 * size.  For iso-maps the width is twice the height since "natural"
 * coordinates are used.  For classical maps the width and height are
 * equal.  The base size may be adjusted to get the correct scale. */
extern int OVERVIEW_TILE_SIZE;
#define OVERVIEW_TILE_WIDTH ((MAP_IS_ISOMETRIC ? 2 : 1) * OVERVIEW_TILE_SIZE)
#define OVERVIEW_TILE_HEIGHT OVERVIEW_TILE_SIZE

/* Tileset accessor functions. */
bool tileset_is_isometric(struct tileset *t);
int tileset_hex_width(struct tileset *t);
int tileset_hex_height(struct tileset *t);
int tileset_tile_width(struct tileset *t);
int tileset_tile_height(struct tileset *t);
int tileset_full_tile_width(struct tileset *t);
int tileset_full_tile_height(struct tileset *t);
int tileset_small_sprite_width(struct tileset *t);
int tileset_small_sprite_height(struct tileset *t);
int tileset_citybar_offset_y(struct tileset *t);
const char *tileset_main_intro_filename(struct tileset *t);
const char *tileset_mini_intro_filename(struct tileset *t);
int tileset_num_city_colors(struct tileset *t);

struct Sprite *load_sprite(struct tileset *t, const char *tag_name);
void unload_sprite(struct tileset *t, const char *tag_name);
bool sprite_exists(struct tileset *t, const char *tag_name);

#endif  /* FC__TILESPEC_H */
