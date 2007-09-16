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

#include "astring.h"
#include "base.h"
#include "capability.h"
#include "fcintl.h"
#include "game.h" /* for fill_xxx */
#include "government.h"
#include "hash.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "nation.h"
#include "player.h"
#include "registry.h"
#include "shared.h"
#include "support.h"
#include "unit.h"
#include "unitlist.h"

#include "citydlg_common.h"
#include "civclient.h"		/* for get_client_state() */
#include "climap.h"		/* for client_tile_get_known() */
#include "control.h"		/* for fill_xxx */
#include "options.h"		/* for fill_xxx */
#include "themes_common.h"


#define TILESPEC_CAPSTR "+tilespec4.2007.Jul.13 duplicates_ok"
/*
 * Tilespec capabilities acceptable to this program:
 *
 * +tilespec4     -  basic format; required
 *
 * duplicates_ok  -  we can handle existence of duplicate tags
 *                   (lattermost tag which appears is used; tilesets which
 *		     have duplicates should specify "+duplicates_ok")
 */

#define SPEC_CAPSTR "+spec3"
/*
 * Individual spec file capabilities acceptable to this program:
 * +spec3          -  basic format, required
 */

#define TILESPEC_SUFFIX ".tilespec"
#define TILE_SECTION_PREFIX "tile_"

/* This the way directional indices are now encoded: */
#define MAX_INDEX_CARDINAL 		64
#define MAX_INDEX_HALF                  16
#define MAX_INDEX_VALID			256

#define NUM_TILES_CITIZEN CITIZEN_LAST
#define NUM_TILES_HP_BAR 11
#define NUM_TILES_DIGITS 10
#define NUM_TILES_SELECT 4
#define MAX_NUM_CITIZEN_SPRITES 6


struct sprite;			/* opaque; gui-dep */

struct base_type;
struct resource;

/* Create the sprite_vector type. */
#define SPECVEC_TAG sprite
#define SPECVEC_TYPE struct sprite *
#include "specvec.h"
#define sprite_vector_iterate(sprite_vec, psprite) \
  TYPED_VECTOR_ITERATE(struct sprite *, sprite_vec, psprite)
#define sprite_vector_iterate_end VECTOR_ITERATE_END

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
  struct sprite *sprite;
  int offset_x, offset_y;	/* offset from tile origin */
};

/* 
 * Information about an individual sprite. All fields except 'sprite' are
 * filled at the time of the scan of the specfile. 'Sprite' is
 * set/cleared on demand in load_sprite/unload_sprite.
 */
struct small_sprite {
  int ref_count;

  /* The sprite is in this file. */
  char *file;

  /* Or, the sprite is in this file at the location. */
  struct specfile *sf;
  int x, y, width, height;

  /* A little more (optional) data. */
  int hot_x, hot_y;

  struct sprite *sprite;
};


#define NUM_CURSOR_FRAMES 6

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

struct citybar_sprites {
  struct sprite
    *shields,
    *food,
    *occupied,
    *background;
  struct sprite_vector occupancy;
};


/* Items on the mapview are drawn in layers.  Each entry below represents
 * one layer.  The names are basically arbitrary and just correspond to
 * groups of elements in fill_sprite_array().  Callers of fill_sprite_array
 * must call it once for each layer. */
enum mapview_layer {
  LAYER_BACKGROUND,
  LAYER_TERRAIN1,
  LAYER_TERRAIN2,
  LAYER_TERRAIN3,
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
  LAYER_CITYBAR,
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

enum arrow_type {
  ARROW_RIGHT,
  ARROW_PLUS,
  ARROW_MINUS,
  ARROW_LAST
};



/* This could be moved to common/map.h if there's more use for it. */
enum direction4 {
  DIR4_NORTH = 0, DIR4_SOUTH, DIR4_EAST, DIR4_WEST
};
static const char direction4letters[4] = "udrl";

static const int DIR4_TO_DIR8[4] =
    { DIR8_NORTH, DIR8_SOUTH, DIR8_EAST, DIR8_WEST };

enum match_style {
  MATCH_NONE,
  MATCH_SAME,		/* "boolean" match */
  MATCH_PAIR,
  MATCH_FULL
};

enum cursor_type {
  CURSOR_GOTO,
  CURSOR_PATROL,
  CURSOR_PARADROP,
  CURSOR_NUKE,
  CURSOR_SELECT,
  CURSOR_INVALID,
  CURSOR_ATTACK,
  CURSOR_EDIT_PAINT,
  CURSOR_EDIT_ADD,
  CURSOR_WAIT,
  CURSOR_LAST,
  CURSOR_DEFAULT,
};

enum sprite_type {
  CELL_WHOLE,		/* entire tile */
  CELL_CORNER		/* corner of tile */
};

struct drawing_data {
  char *name;
  char *mine_tag;

  int num_layers; /* 1 thru MAX_NUM_LAYERS. */
#define MAX_NUM_LAYERS 3

  struct drawing_layer {
    bool is_tall;
    int offset_x, offset_y;

    enum match_style match_style;
    int match_index[1 + MATCH_FULL];
    int match_indices; /* 0 = no match_type, 1 = no match_with */

    enum sprite_type sprite_type;

    struct sprite_vector base;
    struct sprite *match[MAX_INDEX_CARDINAL];
    struct sprite **cells;
  } layer[MAX_NUM_LAYERS];

  bool is_reversed;

  int blending; /* layer, 0 = none */
  struct sprite *blender;
  struct sprite *blend[4]; /* indexed by a direction4 */

  struct sprite *mine;
};

struct city_sprite {
  struct {
    int num_thresholds;
    struct {
      int city_size;
      struct sprite *sprite;
    } *thresholds;
  } *styles;
  int num_styles;
};

struct named_sprites {
  struct sprite
    *indicator[INDICATOR_COUNT][NUM_TILES_PROGRESS],
    *treaty_thumb[2],     /* 0=disagree, 1=agree */
    *arrow[ARROW_LAST], /* 0=right arrow, 1=plus, 2=minus */

    *icon[ICON_COUNT],

    /* The panel sprites for showing tax % allocations. */
    *tax_luxury, *tax_science, *tax_gold,
    *dither_tile;     /* only used for isometric view */

  struct {
    struct sprite
      *tile,
      *worked_tile,
      *unworked_tile;
  } mask;

  struct sprite *tech[A_LAST];
  struct sprite *building[B_LAST];
  struct sprite *government[G_MAGIC];
  struct sprite *unittype[U_LAST];
  struct sprite *resource[MAX_NUM_RESOURCES];

  struct sprite_vector nation_flag;
  struct sprite_vector nation_shield;

  struct citizen_graphic {
    /* Each citizen type has up to MAX_NUM_CITIZEN_SPRITES different
     * sprites, as defined by the tileset. */
    int count;
    struct sprite *sprite[MAX_NUM_CITIZEN_SPRITES];
  } citizen[NUM_TILES_CITIZEN], specialist[SP_MAX];
  struct sprite *spaceship[SPACESHIP_COUNT];
  struct {
    int hot_x, hot_y;
    struct sprite *frame[NUM_CURSOR_FRAMES];
  } cursor[CURSOR_LAST];
  struct {
    struct sprite
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
    struct sprite *nuke;
  } explode;
  struct {
    struct sprite
      *hp_bar[NUM_TILES_HP_BAR],
      *vet_lev[MAX_VET_LEVELS],
      *select[NUM_TILES_SELECT],
      *auto_attack,
      *auto_settler,
      *auto_explore,
      *fallout,
      *fortified,
      *fortifying,
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
      *battlegroup[MAX_NUM_BATTLEGROUPS],
      *lowfuel,
      *tired;
  } unit;
  struct {
    struct sprite
      *unhappy[2],
      *output[O_MAX][2];
  } upkeep;
  struct {
    struct sprite
      *disorder,
      *size[NUM_TILES_DIGITS],
      *size_tens[NUM_TILES_DIGITS],		/* first unused */
      *tile_foodnum[NUM_TILES_DIGITS],
      *tile_shieldnum[NUM_TILES_DIGITS],
      *tile_tradenum[NUM_TILES_DIGITS];
    struct city_sprite
      *tile,
      *wall,
      *occupied;
    struct sprite_vector worked_tile_overlay;
    struct sprite_vector unworked_tile_overlay;
  } city;
  struct citybar_sprites citybar;
  struct {
    struct sprite
      *turns[NUM_TILES_DIGITS],
      *turns_tens[NUM_TILES_DIGITS];
  } path;
  struct {
    struct sprite *attention;
  } user;
  struct {
    struct sprite
      *farmland[MAX_INDEX_CARDINAL],
      *irrigation[MAX_INDEX_CARDINAL],
      *pollution,
      *village,
      *fallout,
      *fog,
      **fullfog,
      *spec_river[MAX_INDEX_CARDINAL],
      *darkness[MAX_INDEX_CARDINAL],         /* first unused */
      *river_outlet[4];		/* indexed by enum direction4 */
  } tx;				/* terrain extra */
  struct {
    struct sprite
      *background,
      *middleground,
      *foreground,
      *activity;
  } bases[BASE_LAST];
  struct {
    struct sprite
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
    struct sprite *player[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];
    struct sprite *background; /* Generic background */
  } backgrounds;
  struct {
    struct sprite_vector overlays;
    struct sprite *background; /* Generic background color */
    struct sprite *player[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];
  } colors;

  struct drawing_data *drawing[MAX_NUM_ITEMS];
};

/* Don't reorder this enum since tilesets depend on it. */
enum fog_style {
  FOG_AUTO, /* Fog is automatically appended by the code. */
  FOG_SPRITE, /* A single fog sprite is provided by the tileset (tx.fog). */
  FOG_NONE /* No fog. */
};

/* Darkness style.  Don't reorder this enum since tilesets depend on it. */
enum darkness_style {
  /* No darkness sprites are drawn. */
  DARKNESS_NONE = 0,

  /* 1 sprite that is split into 4 parts and treated as a darkness4.  Only
   * works in iso-view. */
  DARKNESS_ISORECT = 1,

  /* 4 sprites, one per direction.  More than one sprite per tile may be
   * drawn. */
  DARKNESS_CARD_SINGLE = 2,

  /* 15=2^4-1 sprites.  A single sprite is drawn, chosen based on whether
   * there's darkness in _each_ of the cardinal directions. */
  DARKNESS_CARD_FULL = 3,

  /* Corner darkness & fog.  3^4 = 81 sprites. */
  DARKNESS_CORNER = 4
};

struct specfile {
  struct sprite *big_sprite;
  char *file_name;
};

#define SPECLIST_TAG specfile
#define SPECLIST_TYPE struct specfile
#include "speclist.h"

#define specfile_list_iterate(list, pitem) \
    TYPED_LIST_ITERATE(struct specfile, list, pitem)
#define specfile_list_iterate_end  LIST_ITERATE_END

#define SPECLIST_TAG small_sprite
#define SPECLIST_TYPE struct small_sprite
#include "speclist.h"

#define small_sprite_list_iterate(list, pitem) \
    TYPED_LIST_ITERATE(struct small_sprite, list, pitem)
#define small_sprite_list_iterate_end  LIST_ITERATE_END

struct tileset {
  char name[512];
  int priority;

  bool is_isometric;
  int hex_width, hex_height;

  int normal_tile_width, normal_tile_height;
  int full_tile_width, full_tile_height;
  int small_sprite_width, small_sprite_height;

  char *main_intro_filename;
  char *minimap_intro_filename;

  int city_names_font_size, city_productions_font_size;

  int roadstyle;
  enum fog_style fogstyle;
  enum darkness_style darkness_style;

  int unit_flag_offset_x, unit_flag_offset_y;
  int city_flag_offset_x, city_flag_offset_y;
  int unit_offset_x, unit_offset_y;

  int citybar_offset_y;

#define NUM_CORNER_DIRS 4
#define TILES_PER_CORNER 4
  int num_valid_tileset_dirs, num_cardinal_tileset_dirs;
  int num_index_valid, num_index_cardinal;
  enum direction8 valid_tileset_dirs[8], cardinal_tileset_dirs[8];

  struct tileset_layer {
    char **match_types;
    int match_count;
  } layers[MAX_NUM_LAYERS];

  struct specfile_list *specfiles;
  struct small_sprite_list *small_sprites;

  /*
   * This hash table maps tilespec tags to struct small_sprites.
   */
  struct hash_table *sprite_hash;

  /* This hash table maps terrain graphic strings to drawing data. */
  struct hash_table *tile_hash;

  struct named_sprites sprites;

  struct color_system *color_system;
  
  int num_prefered_themes;
  char** prefered_themes;
};

struct tileset *tileset;


const char **get_tileset_list(void);

struct tileset *tileset_read_toplevel(const char *tileset_name, bool verbose);
void tileset_init(struct tileset *t);
void tileset_free(struct tileset *tileset);
void tileset_load_tiles(struct tileset *t);
void tileset_free_tiles(struct tileset *t);

void tilespec_try_read(const char *tileset_name, bool verbose);
void tilespec_reread(const char *tileset_name);
void tilespec_reread_callback(struct client_option *option);

void tileset_setup_specialist_type(struct tileset *t, Specialist_type_id id);
void tileset_setup_unit_type(struct tileset *t, struct unit_type *punittype);
void tileset_setup_impr_type(struct tileset *t,
			     struct impr_type *pimprove);
void tileset_setup_tech_type(struct tileset *t,
			     struct advance *padvance);
void tileset_setup_tile_type(struct tileset *t,
			     const struct terrain *pterrain);
void tileset_setup_resource(struct tileset *t,
			    const struct resource *presource);
void tileset_setup_base(struct tileset *t,
                        const struct base_type *pbase);
void tileset_setup_government(struct tileset *t,
			      struct government *gov);
void tileset_setup_nation_flag(struct tileset *t, 
			       struct nation_type *nation);
void tileset_setup_city_tiles(struct tileset *t, int style);

/* Gfx support */

int fill_sprite_array(struct tileset *t,
		      struct drawn_sprite *sprs, enum mapview_layer layer,
		      const struct tile *ptile,
		      const struct tile_edge *pedge,
		      const struct tile_corner *pcorner,
		      const struct unit *punit, const struct city *pcity,
		      const struct city *citymode);

double get_focus_unit_toggle_timeout(const struct tileset *t);
void reset_focus_unit_state(struct tileset *t);
void focus_unit_in_combat(struct tileset *t);
void toggle_focus_unit_state(struct tileset *t);
struct unit *get_drawable_unit(const struct tileset *t,
			       struct tile *ptile,
			       const struct city *citymode);




struct sprite *get_spaceship_sprite(const struct tileset *t,
				    enum spaceship_part part);
struct sprite *get_citizen_sprite(const struct tileset *t,
				  struct citizen_type type,
				  int citizen_index,
				  const struct city *pcity);
struct sprite *get_city_flag_sprite(const struct tileset *t,
				    const struct city *pcity);
struct sprite *get_nation_flag_sprite(const struct tileset *t,
				      const struct nation_type *nation);
struct sprite *get_tech_sprite(const struct tileset *t, Tech_type_id tech);
struct sprite *get_building_sprite(const struct tileset *t,
				   struct impr_type *pimprove);
struct sprite *get_government_sprite(const struct tileset *t,
				     const struct government *gov);
struct sprite *get_unittype_sprite(const struct tileset *t,
				   const struct unit_type *punittype);
struct sprite *get_sample_city_sprite(const struct tileset *t,
				      int city_style);
struct sprite *get_arrow_sprite(const struct tileset *t,
				enum arrow_type arrow);
struct sprite *get_tax_sprite(const struct tileset *t, Output_type_id otype);
struct sprite *get_treaty_thumb_sprite(const struct tileset *t, bool on_off);
const struct sprite_vector *get_unit_explode_animation(const struct
						       tileset *t);
struct sprite *get_nuke_explode_sprite(const struct tileset *t);
struct sprite *get_cursor_sprite(const struct tileset *t,
				 enum cursor_type cursor,
				 int *hot_x, int *hot_y, int frame);
const struct citybar_sprites *get_citybar_sprites(const struct tileset *t);
struct sprite *get_icon_sprite(const struct tileset *t, enum icon_type icon);
struct sprite *get_attention_crosshair_sprite(const struct tileset *t);
struct sprite *get_indicator_sprite(const struct tileset *t,
				    enum indicator_type indicator,
				    int index);
struct sprite *get_unit_unhappy_sprite(const struct tileset *t,
				       const struct unit *punit,
				       int happy_cost);
struct sprite *get_unit_upkeep_sprite(const struct tileset *t,
				      Output_type_id otype,
				      const struct unit *punit,
				      const int *upkeep_cost);
struct sprite *get_basic_fog_sprite(const struct tileset *t);

struct sprite* lookup_sprite_tag_alt(struct tileset *t,
					    const char *tag, const char *alt,
					    bool required, const char *what,
					    const char *name);

struct color_system;
struct color_system *get_color_system(const struct tileset *t);

/* Tileset accessor functions. */
const char *tileset_get_name(const struct tileset *t);
bool tileset_is_isometric(const struct tileset *t);
int tileset_hex_width(const struct tileset *t);
int tileset_hex_height(const struct tileset *t);
int tileset_tile_width(const struct tileset *t);
int tileset_tile_height(const struct tileset *t);
int tileset_full_tile_width(const struct tileset *t);
int tileset_full_tile_height(const struct tileset *t);
int tileset_small_sprite_width(const struct tileset *t);
int tileset_small_sprite_height(const struct tileset *t);
int tileset_citybar_offset_y(const struct tileset *t);
const char *tileset_main_intro_filename(const struct tileset *t);
const char *tileset_mini_intro_filename(const struct tileset *t);
int tileset_num_city_colors(const struct tileset *t);
void tileset_use_prefered_theme(const struct tileset *t);

#endif  /* FC__TILESPEC_H */
