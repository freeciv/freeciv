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
  Functions for handling the tilespec files which describe
  the files and contents of tilesets.
  original author: David Pfitzner <dwp@mso.anu.edu.au>
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdio.h>
#include <stdlib.h>		/* exit */
#include <string.h>

/* utility */
#include "astring.h"
#include "bitvector.h"
#include "capability.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "registry.h"
#include "shared.h"
#include "string_vector.h"
#include "support.h"

/* common */
#include "base.h"
#include "effects.h"
#include "game.h"		/* game.control.styles_count */
#include "government.h"
#include "map.h"
#include "movement.h"
#include "nation.h"
#include "player.h"
#include "road.h"
#include "specialist.h"
#include "unit.h"
#include "unitlist.h"

/* client/include */
#include "dialogs_g.h"
#include "graphics_g.h"
#include "gui_main_g.h"
#include "mapview_g.h"          /* for update_map_canvas_visible */
#include "menu_g.h"
#include "themes_g.h"

/* client */
#include "citydlg_common.h"	/* for generate_citydlg_dimensions() */
#include "client_main.h"
#include "climap.h"		/* for client_tile_get_known() */
#include "colors_common.h"
#include "control.h"		/* for fill_xxx */
#include "editor.h"
#include "goto.h"
#include "options.h"		/* for fill_xxx */
#include "themes_common.h"

#include "tilespec.h"

#define TILESPEC_CAPSTR "+Freeciv-tilespec-Devel-2013.Feb.04 duplicates_ok"
/*
 * Tilespec capabilities acceptable to this program:
 *
 * +Freeciv-2.4-tilespec
 *    - basic format for Freeciv versions 2.4.x; required
 *
 * +Freeciv-tilespec-Devel-YYYY.MMM.DD
 *    - tilespec of the development version at the given data
 *
 * duplicates_ok
 *    - we can handle existence of duplicate tags (lattermost tag which
 *      appears is used; tilesets which have duplicates should specify
 *      "duplicates_ok")
 */

#define SPEC_CAPSTR "+Freeciv-spec-Devel-2013.Feb.13"
/*
 * Individual spec file capabilities acceptable to this program:
 *
 * +Freeciv-2.3-spec
 *    - basic format for Freeciv versions 2.3.x; required
 */

#define TILESPEC_SUFFIX ".tilespec"
#define TILE_SECTION_PREFIX "tile_"

/* This the way directional indices are now encoded: */
#define MAX_INDEX_CARDINAL 		64
#define MAX_INDEX_HALF                  16
#define MAX_INDEX_VALID			256

#define NUM_TILES_HP_BAR 11
#define NUM_TILES_DIGITS 10
#define NUM_TILES_SELECT 4
#define MAX_NUM_CITIZEN_SPRITES 6

#define SPECENUM_NAME roadstyle_id
#define SPECENUM_VALUE0 RSTYLE_ALL_SEPARATE
#define SPECENUM_VALUE0NAME "AllSeparate"
#define SPECENUM_VALUE1 RSTYLE_PARITY_COMBINED
#define SPECENUM_VALUE1NAME "ParityCombined"
#define SPECENUM_VALUE2 RSTYLE_ALL_COMBINED
#define SPECENUM_VALUE2NAME "AllCombined"
#define SPECENUM_VALUE3 RSTYLE_RIVER
#define SPECENUM_VALUE3NAME "River"
#include "specenum_gen.h"

/* This could be moved to common/map.h if there's more use for it. */
enum direction4 {
  DIR4_NORTH = 0, DIR4_SOUTH, DIR4_EAST, DIR4_WEST
};
static const char direction4letters[4] = "udrl";
/* This must correspond to enum edge_type. */
static const char edge_name[EDGE_COUNT][3] = {"ns", "we", "ud", "lr"};

static const int DIR4_TO_DIR8[4] =
    { DIR8_NORTH, DIR8_SOUTH, DIR8_EAST, DIR8_WEST };

enum match_style {
  MATCH_NONE,
  MATCH_SAME,		/* "boolean" match */
  MATCH_PAIR,
  MATCH_FULL
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

#define MAX_NUM_MATCH_WITH 8
    enum match_style match_style;
    int match_index[1 + MAX_NUM_MATCH_WITH];
    int match_indices; /* 0 = no match_type, 1 = no match_with */

    enum sprite_type sprite_type;

    struct sprite_vector base;
    struct sprite *match[MAX_INDEX_CARDINAL];
    struct sprite **cells;

    /* List of those sprites in 'cells' that are allocated by some other
     * means than load_sprite() and thus are not freed by unload_all_sprites(). */
    struct sprite_vector allocated;
  } layer[MAX_NUM_LAYERS];

  bool is_reversed;

  int blending; /* layer, 0 = none */
  struct sprite *blender;
  struct sprite *blend[4]; /* indexed by a direction4 */

  struct sprite *mine;
};

struct city_style_threshold {
  struct sprite *sprite;
};

struct city_sprite {
  struct {
    int land_num_thresholds;
    struct city_style_threshold *land_thresholds;
    int oceanic_num_thresholds;
    struct city_style_threshold *oceanic_thresholds;
  } *styles;
  int num_styles;
};

struct river_sprites {
  struct sprite
    *spec[MAX_INDEX_CARDINAL],
    *outlet[4];		/* indexed by enum direction4 */
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

  struct {
    struct sprite *icon[U_LAST];
    struct sprite *facing[U_LAST][DIR8_MAGIC_MAX]; 
  } units;

  struct sprite *resource[MAX_NUM_RESOURCES];

  struct sprite_vector nation_flag;
  struct sprite_vector nation_shield;

  struct citizen_graphic {
    /* Each citizen type has up to MAX_NUM_CITIZEN_SPRITES different
     * sprites, as defined by the tileset. */
    int count;
    struct sprite *sprite[MAX_NUM_CITIZEN_SPRITES];
  } citizen[CITIZEN_LAST], specialist[SP_MAX];
  struct sprite *spaceship[SPACESHIP_COUNT];
  struct {
    int hot_x, hot_y;
    struct sprite *frame[NUM_CURSOR_FRAMES];
  } cursor[CURSOR_LAST];
  struct {
    enum roadstyle_id roadstyle;
    struct sprite
      *activity,
      /* for roadstyles RSTYLE_ALL_SEPARATE and RSTYLE_PARITY_COMBINED */
      *isolated,
      *corner[8]; /* Indexed by direction; only non-cardinal dirs used. */
    union {
      /* for RSTYLE_ALL_SEPARATE */
      struct sprite *dir[8];     /* all entries used */
      /* RSTYLE_PARITY_COMBINED */
      struct {
        struct sprite
          *even[MAX_INDEX_HALF],    /* first unused */
          *odd[MAX_INDEX_HALF];     /* first unused */
      } combo;
      /* RSTYLE_ALL_SEPARATE */
      struct sprite *total[MAX_INDEX_VALID];
      struct river_sprites rivers;
    } u;
  } roads[MAX_ROAD_TYPES];
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
      *sentry,
      *stack,
      *loaded,
      *transform,
      *connect,
      *patrol,
      *convert,
      *battlegroup[MAX_NUM_BATTLEGROUPS],
      *lowfuel,
      *tired;
  } unit;
  struct {
    struct sprite
      *unhappy[2],
      *output[O_LAST][2];
  } upkeep;
  struct {
    struct sprite
      *disorder,
      *size[NUM_TILES_DIGITS],
      *size_tens[NUM_TILES_DIGITS],
      *size_hundreds[NUM_TILES_DIGITS],
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
  struct editor_sprites editor;
  struct {
    struct sprite
      *turns[NUM_TILES_DIGITS],
      *turns_tens[NUM_TILES_DIGITS],
      *turns_hundreds[NUM_TILES_DIGITS];
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
      *darkness[MAX_INDEX_CARDINAL]; /* first unused */
    struct river_sprites rivers;
  } tx;				/* terrain extra */
  struct {
    struct sprite
      *background,
      *middleground,
      *foreground,
      *activity;
  } bases[MAX_BASE_TYPES];
  struct {
    struct sprite
      *main[EDGE_COUNT],
      *city[EDGE_COUNT],
      *worked[EDGE_COUNT],
      *unavailable,
      *nonnative,
      *selected[EDGE_COUNT],
      *coastline[EDGE_COUNT],
      *borders[EDGE_COUNT][2];
  } grid;
  struct {
    struct sprite_vector overlays;
  } colors;
  struct {
    struct sprite *color; /* Generic background color */
    struct sprite *graphic; /* Generic background graphic */
  } background;
  struct {
    struct sprite *grid_borders[EDGE_COUNT][2];
    struct sprite *color;
    struct sprite *background;
  } player[MAX_NUM_PLAYER_SLOTS];

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

/* 'struct small_sprite_list' and related functions. */
#define SPECLIST_TAG small_sprite
#define SPECLIST_TYPE struct small_sprite
#include "speclist.h"
#define small_sprite_list_iterate(list, pitem)                              \
  TYPED_LIST_ITERATE(struct small_sprite, list, pitem)
#define small_sprite_list_iterate_end LIST_ITERATE_END

/* 'struct sprite_hash' and related functions. */
#define SPECHASH_TAG sprite
#define SPECHASH_KEY_TYPE char *
#define SPECHASH_DATA_TYPE struct small_sprite *
#define SPECHASH_KEY_VAL genhash_str_val_func
#define SPECHASH_KEY_COMP genhash_str_comp_func
#define SPECHASH_KEY_COPY genhash_str_copy_func
#define SPECHASH_KEY_FREE genhash_str_free_func
#include "spechash.h"
#define sprite_hash_iterate(hash, tag_name, sprite)                         \
  TYPED_HASH_ITERATE(const char *, struct small_sprite *,                   \
                     hash, tag_name, sprite)
#define sprite_hash_iterate_end HASH_ITERATE_END

/* 'struct drawing_hash' and related functions. */
static void drawing_data_destroy(struct drawing_data *draw);

#define SPECHASH_TAG drawing
#define SPECHASH_KEY_TYPE char *
#define SPECHASH_DATA_TYPE struct drawing_data *
#define SPECHASH_KEY_VAL genhash_str_val_func
#define SPECHASH_KEY_COMP genhash_str_comp_func
#define SPECHASH_DATA_FREE drawing_data_destroy
#include "spechash.h"

#define SPECHASH_TAG rstyle
#define SPECHASH_KEY_TYPE char *
#define SPECHASH_DATA_TYPE enum roadstyle_id *
#define SPECHASH_KEY_VAL genhash_str_val_func
#define SPECHASH_KEY_COMP genhash_str_comp_func
#include "spechash.h"

enum ts_type { TS_OVERVIEW, TS_ISOMETRIC };

struct tileset {
  char name[512];
  int priority;

  enum ts_type type;
  int hex_width, hex_height;

  int normal_tile_width, normal_tile_height;
  int full_tile_width, full_tile_height;
  int unit_tile_width, unit_tile_height;
  int small_sprite_width, small_sprite_height;

  char *main_intro_filename;
  char *minimap_intro_filename;

  int city_names_font_size, city_productions_font_size;

  enum fog_style fogstyle;
  enum darkness_style darkness_style;

  int unit_flag_offset_x, unit_flag_offset_y;
  int city_flag_offset_x, city_flag_offset_y;
  int unit_offset_x, unit_offset_y;

  int citybar_offset_y;
  int tilelabel_offset_y;

#define NUM_CORNER_DIRS 4
#define TILES_PER_CORNER 4
  int num_valid_tileset_dirs, num_cardinal_tileset_dirs;
  int num_index_valid, num_index_cardinal;
  enum direction8 valid_tileset_dirs[8], cardinal_tileset_dirs[8];

  struct tileset_layer {
    char **match_types;
    size_t match_count;
  } layers[MAX_NUM_LAYERS];

  struct specfile_list *specfiles;
  struct small_sprite_list *small_sprites;

  /* This hash table maps tilespec tags to struct small_sprites. */
  struct sprite_hash *sprite_hash;

  /* This hash table maps terrain graphic strings to drawing data. */
  struct drawing_hash *tile_hash;

  struct rstyle_hash *rstyle_hash;

  struct named_sprites sprites;

  struct color_system *color_system;

  struct road_type_list *rivers;

  int num_prefered_themes;
  char** prefered_themes;
};

struct tileset *tileset;

int focus_unit_state = 0;


static int fill_unit_type_sprite_array(const struct tileset *t,
                                       struct drawn_sprite *sprs,
                                       const struct unit_type *putype,
                                       enum direction8 facing);
static int fill_unit_sprite_array(const struct tileset *t,
                                  struct drawn_sprite *sprs,
                                  const struct unit *punit,
                                  bool stack, bool backdrop);
static void load_river_sprites(struct tileset *t,
                               struct river_sprites *store, const char *tag_pfx);


/****************************************************************************
  Create a new drawing data.
****************************************************************************/
static struct drawing_data *drawing_data_new(const char *name)
{
  struct drawing_data *draw = fc_calloc(1, sizeof(*draw));

  draw->name = fc_strdup(name);
  return draw;
}

/****************************************************************************
  Free a drawing data.
****************************************************************************/
static void drawing_data_destroy(struct drawing_data *draw)
{
  int i;

  fc_assert_ret(NULL != draw);

  free(draw->name);
  if (NULL != draw->mine_tag) {
    free(draw->mine_tag);
  }
  for (i = 0; i < 4; i++) {
    if (draw->blend[i]) {
      free_sprite(draw->blend[i]);
    }
  }
  for (i = 0; i < draw->num_layers; i++) {
    int vec_size = sprite_vector_size(&draw->layer[i].allocated);
    int j;

    for (j = 0; j < vec_size; j++) {
      free_sprite(draw->layer[i].allocated.p[j]);
    }

    sprite_vector_free(&draw->layer[i].base);
    sprite_vector_free(&draw->layer[i].allocated);
  }
  free(draw);
}

/****************************************************************************
  Return the name of the given tileset.
****************************************************************************/
const char *tileset_get_name(const struct tileset *t)
{
  return t->name;
}

/****************************************************************************
  Return whether the current tileset is isometric.
****************************************************************************/
bool tileset_is_isometric(const struct tileset *t)
{
  return t->type == TS_ISOMETRIC;
}

/****************************************************************************
  Return the hex_width of the current tileset.  For hex tilesets this value
  will be > 0 and is_isometric will be set.
****************************************************************************/
int tileset_hex_width(const struct tileset *t)
{
  return t->hex_width;
}

/****************************************************************************
  Return the hex_height of the current tileset.  For iso-hex tilesets this
  value will be > 0 and is_isometric will be set.
****************************************************************************/
int tileset_hex_height(const struct tileset *t)
{
  return t->hex_height;
}

/****************************************************************************
  Return the tile width of the current tileset.  This is the tesselation
  width of the tiled plane.  This means it's the width of the bounding box
  of the basic map tile.

  For best results:
    - The value should be even (or a multiple of 4 in iso-view).
    - In iso-view, the width should be twice the height (to give a
      perspective of 30 degrees above the horizon).
    - In non-iso-view, width and height should be equal (overhead
      perspective).
    - In hex or iso-hex view, remember this is the tesselation vector.
      hex_width and hex_height then give the size of the side of the
      hexagon.  Calculating the dimensions of a "regular" hexagon or
      iso-hexagon may be tricky.
  However these requirements are not absolute and callers should not
  depend on them (although some do).
****************************************************************************/
int tileset_tile_width(const struct tileset *t)
{
  return t->normal_tile_width;
}

/****************************************************************************
  Return the tile height of the current tileset.  This is the tesselation
  height of the tiled plane.  This means it's the height of the bounding box
  of the basic map tile.

  See also tileset_tile_width.
****************************************************************************/
int tileset_tile_height(const struct tileset *t)
{
  return t->normal_tile_height;
}

/****************************************************************************
  Return the full tile width of the current tileset.  This is the maximum
  width that any mapview sprite will have.

  Note: currently this is always equal to the tile width.
****************************************************************************/
int tileset_full_tile_width(const struct tileset *t)
{
  return t->full_tile_width;
}

/****************************************************************************
  Return the full tile height of the current tileset.  This is the maximum
  height that any mapview sprite will have.  This may be greater than the
  tile width in which case the extra area is above the "normal" tile.

  Some callers assume the full height is 50% larger than the height in
  iso-view, and equal in non-iso view.
****************************************************************************/
int tileset_full_tile_height(const struct tileset *t)
{
  return t->full_tile_height;
}

/****************************************************************************
  Return the unit tile width of the current tileset.
****************************************************************************/
int tileset_unit_width(const struct tileset *t)
{
  return t->unit_tile_width;
}

/****************************************************************************
  Return the unit tile height of the current tileset.
****************************************************************************/
int tileset_unit_height(const struct tileset *t)
{
  return t->unit_tile_height;
}

/****************************************************************************
  Return the small sprite width of the current tileset.  The small sprites
  are used for various theme graphics (e.g., citymap citizens/specialists
  as well as panel indicator icons).
****************************************************************************/
int tileset_small_sprite_width(const struct tileset *t)
{
  return t->small_sprite_width;
}

/****************************************************************************
  Return the offset from the origin of the city tile at which to place the
  city bar text.
****************************************************************************/
int tileset_citybar_offset_y(const struct tileset *t)
{
  return t->citybar_offset_y;
}

/****************************************************************************
  Return the offset from the origin of the tile at which to place the
  label text.
****************************************************************************/
int tileset_tilelabel_offset_y(const struct tileset *t)
{
  return t->tilelabel_offset_y;
}

/****************************************************************************
  Return the small sprite height of the current tileset.  The small sprites
  are used for various theme graphics (e.g., citymap citizens/specialists
  as well as panel indicator icons).
****************************************************************************/
int tileset_small_sprite_height(const struct tileset *t)
{
  return t->small_sprite_height;
}

/****************************************************************************
  Return the path within the data directories where the main intro graphics
  file can be found.  (It is left up to the GUI code to load and unload this
  file.)
****************************************************************************/
const char *tileset_main_intro_filename(const struct tileset *t)
{
  return t->main_intro_filename;
}

/****************************************************************************
  Return the path within the data directories where the mini intro graphics
  file can be found.  (It is left up to the GUI code to load and unload this
  file.)
****************************************************************************/
const char *tileset_mini_intro_filename(const struct tileset *t)
{
  return t->minimap_intro_filename;
}

/****************************************************************************
  Return the number of possible colors for city overlays.
****************************************************************************/
int tileset_num_city_colors(const struct tileset *t)
{
  return t->sprites.city.worked_tile_overlay.size;
}

/****************************************************************************
  Return TRUE if the client will use the code to generate the fog.
****************************************************************************/
bool tileset_use_hard_coded_fog(const struct tileset *t)
{
  return FOG_AUTO == t->fogstyle;
}

/**************************************************************************
  Initialize.
**************************************************************************/
static struct tileset *tileset_new(void)
{
  struct tileset *t = fc_calloc(1, sizeof(*t));

  t->specfiles = specfile_list_new();
  t->small_sprites = small_sprite_list_new();
  return t;
}

/**************************************************************************
  Return the tileset name of the direction.  This is similar to
  dir_get_name but you shouldn't change this or all tilesets will break.
**************************************************************************/
static const char *dir_get_tileset_name(enum direction8 dir)
{
  switch (dir) {
  case DIR8_NORTH:
    return "n";
  case DIR8_NORTHEAST:
    return "ne";
  case DIR8_EAST:
    return "e";
  case DIR8_SOUTHEAST:
    return "se";
  case DIR8_SOUTH:
    return "s";
  case DIR8_SOUTHWEST:
    return "sw";
  case DIR8_WEST:
    return "w";
  case DIR8_NORTHWEST:
    return "nw";
  }
  log_error("Wrong direction8 variant: %d.", dir);
  return "";
}

/****************************************************************************
  Return TRUE iff the dir is valid in this tileset.
****************************************************************************/
static bool is_valid_tileset_dir(const struct tileset *t,
				 enum direction8 dir)
{
  if (t->hex_width > 0) {
    return dir != DIR8_NORTHEAST && dir != DIR8_SOUTHWEST;
  } else if (t->hex_height > 0) {
    return dir != DIR8_NORTHWEST && dir != DIR8_SOUTHEAST;
  } else {
    return TRUE;
  }
}

/****************************************************************************
  Return TRUE iff the dir is cardinal in this tileset.

  "Cardinal", in this sense, means that a tile will share a border with
  another tile in the direction rather than sharing just a single vertex.
****************************************************************************/
static bool is_cardinal_tileset_dir(const struct tileset *t,
				    enum direction8 dir)
{
  if (t->hex_width > 0 || t->hex_height > 0) {
    return is_valid_tileset_dir(t, dir);
  } else {
    return (dir == DIR8_NORTH || dir == DIR8_EAST
	    || dir == DIR8_SOUTH || dir == DIR8_WEST);
  }
}

/**********************************************************************
  Returns a static list of tilesets available on the system by
  searching all data directories for files matching TILESPEC_SUFFIX.
***********************************************************************/
const struct strvec *get_tileset_list(void)
{
  static struct strvec *tilesets = NULL;

  if (!tilesets) {
    /* Note: this means you must restart the client after installing a new
       tileset. */
    struct strvec *list = fileinfolist(get_data_dirs(), TILESPEC_SUFFIX);

    tilesets = strvec_new();
    strvec_iterate(list, file) {
      struct tileset *t = tileset_read_toplevel(file, FALSE);

      if (t) {
        strvec_append(tilesets, file);
        tileset_free(t);
      }
    } strvec_iterate_end;
    strvec_destroy(list);
  }

  return tilesets;
}

/**********************************************************************
  Gets full filename for tilespec file, based on input name.
  Returned data is allocated, and freed by user as required.
  Input name may be null, in which case uses default.
  Falls back to default if can't find specified name;
  dies if can't find default.
***********************************************************************/
static char *tilespec_fullname(const char *tileset_name)
{
  if (tileset_name) {
    char fname[strlen(tileset_name) + strlen(TILESPEC_SUFFIX) + 1];
    const char *dname;

    fc_snprintf(fname, sizeof(fname),
                "%s%s", tileset_name, TILESPEC_SUFFIX);

    dname = fileinfoname(get_data_dirs(), fname);

    if (dname) {
      return fc_strdup(dname);
    }
  }

  return NULL;
}

/**********************************************************************
  Checks options in filename match what we require and support.
  Die if not.
  'which' should be "tilespec" or "spec".
***********************************************************************/
static bool check_tilespec_capabilities(struct section_file *file,
					const char *which,
					const char *us_capstr,
					const char *filename,
                                        bool verbose)
{
  enum log_level level = verbose ? LOG_ERROR : LOG_DEBUG;

  const char *file_capstr = secfile_lookup_str(file, "%s.options", which);

  if (NULL == file_capstr) {
    log_base(level, "\"%s\": %s file doesn't have a capability string",
             filename, which);
    return FALSE;
  }
  if (!has_capabilities(us_capstr, file_capstr)) {
    log_base(level, "\"%s\": %s file appears incompatible:",
             filename, which);
    log_base(level, "  datafile options: %s", file_capstr);
    log_base(level, "  supported options: %s", us_capstr);
    return FALSE;
  }
  if (!has_capabilities(file_capstr, us_capstr)) {
    log_base(level, "\"%s\": %s file requires option(s) "
             "that client doesn't support:", filename, which);
    log_base(level, "  datafile options: %s", file_capstr);
    log_base(level, "  supported options: %s", us_capstr);
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************
  Frees the tilespec toplevel data, in preparation for re-reading it.

  See tilespec_read_toplevel().
***********************************************************************/
static void tileset_free_toplevel(struct tileset *t)
{
  int i, j;

  if (t->main_intro_filename) {
    free(t->main_intro_filename);
    t->main_intro_filename = NULL;
  }
  if (t->minimap_intro_filename) {
    free(t->minimap_intro_filename);
    t->minimap_intro_filename = NULL;
  }
  
  if (t->prefered_themes) {
    for (i = 0; i < t->num_prefered_themes; i++) {
      free(t->prefered_themes[i]);
    }
    free(t->prefered_themes);
    t->prefered_themes = NULL;
  }
  t->num_prefered_themes = 0;

  if (t->tile_hash) {
    drawing_hash_destroy(t->tile_hash);
    t->tile_hash = NULL; /* Helpful for sanity. */
  }
  if (t->rstyle_hash) {
    rstyle_hash_destroy(t->rstyle_hash);
    t->rstyle_hash = NULL;
  }
  if (t->rivers != NULL) {
    road_type_list_destroy(t->rivers);
    t->rivers = NULL;
  }
 
  for (i = 0; i < MAX_NUM_LAYERS; i++) {
    struct tileset_layer *tslp = &t->layers[i];

    if (tslp->match_types) {
      for (j = 0; j < tslp->match_count; j++) {
	free(tslp->match_types[j]);
      }
      free(tslp->match_types);
      tslp->match_types = NULL;
    }
  }

  if (t->color_system) {
    color_system_free(t->color_system);
    t->color_system = NULL;
  }
}

/**************************************************************************
  Clean up.
**************************************************************************/
void tileset_free(struct tileset *t)
{
  tileset_free_tiles(t);
  tileset_free_toplevel(t);
  specfile_list_destroy(t->specfiles);
  small_sprite_list_destroy(t->small_sprites);
  free(t);
}

/**********************************************************************
  Read a new tilespec in when first starting the game.

  Call this function with the (guessed) name of the tileset, when
  starting the client.
***********************************************************************/
void tilespec_try_read(const char *tileset_name, bool verbose)
{
  if (!(tileset = tileset_read_toplevel(tileset_name, verbose))) {
    struct strvec *list = fileinfolist(get_data_dirs(), TILESPEC_SUFFIX);

    strvec_iterate(list, file) {
      struct tileset *t = tileset_read_toplevel(file, FALSE);

      if (t) {
        if (!tileset || t->priority > tileset->priority) {
          tileset = t;
        } else {
          tileset_free(t);
        }
      }
    } strvec_iterate_end;
    strvec_destroy(list);

    if (!tileset) {
      log_fatal(_("No usable default tileset found, aborting!"));
      exit(EXIT_FAILURE);
    }

    log_verbose("Trying tileset \"%s\".", tileset->name);
  }
  sz_strlcpy(default_tileset_name, tileset_get_name(tileset));
}

/**********************************************************************
  Read a new tilespec in from scratch.

  Unlike the initial reading code, which reads pieces one at a time,
  this gets rid of the old data and reads in the new all at once.  If the
  new tileset fails to load the old tileset may be reloaded; otherwise the
  client will exit.  If a NULL name is given the current tileset will be
  reread.

  It will also call the necessary functions to redraw the graphics.
***********************************************************************/
void tilespec_reread(const char *new_tileset_name)
{
  int id;
  struct tile *center_tile;
  enum client_states state = client_state();
  const char *name = new_tileset_name ? new_tileset_name : tileset->name;
  char tileset_name[strlen(name) + 1], old_name[strlen(tileset->name) + 1];

  /* Make local copies since these values may be freed down below */
  sz_strlcpy(tileset_name, name);
  sz_strlcpy(old_name, tileset->name);

  log_normal(_("Loading tileset \"%s\"."), tileset_name);

  /* Step 0:  Record old data.
   *
   * We record the current mapcanvas center, etc.
   */
  center_tile = get_center_tile_mapcanvas();

  /* Step 1:  Cleanup.
   *
   * We free all old data in preparation for re-reading it.
   */
  tileset_free_tiles(tileset);
  tileset_free_toplevel(tileset);
  players_iterate(pplayer) {
    tileset_player_free(tileset, pplayer);
  } players_iterate_end;

  /* Step 2:  Read.
   *
   * We read in the new tileset.  This should be pretty straightforward.
   */
  if (!(tileset = tileset_read_toplevel(tileset_name, FALSE))) {
    if (!(tileset = tileset_read_toplevel(old_name, FALSE))) {
      /* Always fails. */
      fc_assert_exit_msg(NULL != tileset,
                         "Failed to re-read the currently loaded tileset.");
    }
  }
  sz_strlcpy(default_tileset_name, tileset->name);
  tileset_load_tiles(tileset);
  tileset_use_prefered_theme(tileset);
  players_iterate(pplayer) {
    tileset_player_init(tileset, pplayer);
  } players_iterate_end;

  /* Step 3: Setup
   *
   * This is a seriously sticky problem.  On startup, we build a hash
   * from all the sprite data. Then, when we connect to a server, the
   * server sends us ruleset data a piece at a time and we use this data
   * to assemble the sprite structures.  But if we change while connected
   *  we have to reassemble all of these.  This should just involve
   * calling tilespec_setup_*** on everything.  But how do we tell what
   * "everything" is?
   *
   * The below code just does things straightforwardly, by setting up
   * each possible sprite again.  Hopefully it catches everything, and
   * doesn't mess up too badly if we change tilesets while not connected
   * to a server.
   */
  if (state < C_S_RUNNING) {
    /* The ruleset data is not sent until this point. */
    return;
  }
  terrain_type_iterate(pterrain) {
    tileset_setup_tile_type(tileset, pterrain);
  } terrain_type_iterate_end;
  resource_type_iterate(presource) {
    tileset_setup_resource(tileset, presource);
  } resource_type_iterate_end;
  unit_type_iterate(punittype) {
    tileset_setup_unit_type(tileset, punittype);
  } unit_type_iterate_end;
  governments_iterate(gov) {
    tileset_setup_government(tileset, gov);
  } governments_iterate_end;
  road_type_iterate(proad) {
    tileset_setup_road(tileset, proad);
  } road_type_iterate_end;
  base_type_iterate(pbase) {
    tileset_setup_base(tileset, pbase);
  } base_type_iterate_end;
  nations_iterate(pnation) {
    tileset_setup_nation_flag(tileset, pnation);
  } nations_iterate_end;
  improvement_iterate(pimprove) {
    tileset_setup_impr_type(tileset, pimprove);
  } improvement_iterate_end;
  advance_iterate(A_FIRST, padvance) {
    tileset_setup_tech_type(tileset, padvance);
  } advance_iterate_end;
  specialist_type_iterate(sp) {
    tileset_setup_specialist_type(tileset, sp);
  } specialist_type_iterate_end;

  for (id = 0; id < game.control.styles_count; id++) {
    tileset_setup_city_tiles(tileset, id);
  }

  /* Step 4:  Draw.
   *
   * Do any necessary redraws.
   */
  generate_citydlg_dimensions();
  tileset_changed();
  can_slide = FALSE;
  center_tile_mapcanvas(center_tile);
  /* update_map_canvas_visible forces a full redraw.  Otherwise with fast
   * drawing we might not get one.  Of course this is slower. */
  update_map_canvas_visible();
  can_slide = TRUE;
}

/**************************************************************************
  This is merely a wrapper for tilespec_reread (above) for use in
  options.c and the client local options dialog.
**************************************************************************/
void tilespec_reread_callback(struct option *poption)
{
  const char *tileset_name = option_str_get(poption);

  fc_assert_ret(NULL != tileset_name && tileset_name[0] != '\0');
  tilespec_reread(tileset_name);
  menus_init();
}

/**************************************************************************
  Loads the given graphics file (found in the data path) into a newly
  allocated sprite.
**************************************************************************/
static struct sprite *load_gfx_file(const char *gfx_filename)
{
  const char **gfx_fileexts = gfx_fileextensions(), *gfx_fileext;
  struct sprite *s;

  /* Try out all supported file extensions to find one that works. */
  while ((gfx_fileext = *gfx_fileexts++)) {
    const char *real_full_name;
    char full_name[strlen(gfx_filename) + strlen(gfx_fileext) + 2];

    sprintf(full_name, "%s.%s", gfx_filename, gfx_fileext);
    if ((real_full_name = fileinfoname(get_data_dirs(), full_name))) {
      log_debug("trying to load gfx file \"%s\".", real_full_name);
      s = load_gfxfile(real_full_name);
      if (s) {
	return s;
      }
    }
  }

  log_error("Could not load gfx file \"%s\".", gfx_filename);
  return NULL;
}

/**************************************************************************
  Ensure that the big sprite of the given spec file is loaded.
**************************************************************************/
static void ensure_big_sprite(struct specfile *sf)
{
  struct section_file *file;
  const char *gfx_filename;

  if (sf->big_sprite) {
    /* Looks like it's already loaded. */
    return;
  }

  /* Otherwise load it.  The big sprite will sometimes be freed and will have
   * to be reloaded, but most of the time it's just loaded once, the small
   * sprites are extracted, and then it's freed. */
  if (!(file = secfile_load(sf->file_name, TRUE))) {
    log_fatal(_("Could not open '%s':\n%s"), sf->file_name, secfile_error());
    exit(EXIT_FAILURE);
  }

  if (!check_tilespec_capabilities(file, "spec",
				   SPEC_CAPSTR, sf->file_name, TRUE)) {
    exit(EXIT_FAILURE);
  }

  gfx_filename = secfile_lookup_str(file, "file.gfx");

  sf->big_sprite = load_gfx_file(gfx_filename);

  if (!sf->big_sprite) {
    log_fatal("Could not load gfx file for the spec file \"%s\".",
              sf->file_name);
    exit(EXIT_FAILURE);
  }
  secfile_destroy(file);
}

/**************************************************************************
  Scan all sprites declared in the given specfile.  This means that the
  positions of the sprites in the big_sprite are saved in the
  small_sprite structs.
**************************************************************************/
static void scan_specfile(struct tileset *t, struct specfile *sf,
			  bool duplicates_ok)
{
  struct section_file *file;
  struct section_list *sections;
  int i;

  if (!(file = secfile_load(sf->file_name, TRUE))) {
    log_fatal(_("Could not open '%s':\n%s"), sf->file_name, secfile_error());
    exit(EXIT_FAILURE);
  }
  if (!check_tilespec_capabilities(file, "spec",
				   SPEC_CAPSTR, sf->file_name, TRUE)) {
    exit(EXIT_FAILURE);
  }

  /* currently unused */
  (void) secfile_entry_lookup(file, "info.artists");

  if ((sections = secfile_sections_by_name_prefix(file, "grid_"))) {
    section_list_iterate(sections, psection) {
      int j, k;
      int x_top_left, y_top_left, dx, dy;
      int pixel_border;
      const char *sec_name = section_name(psection);

      pixel_border = secfile_lookup_int_default(file, 0, "%s.pixel_border",
                                                sec_name);

      if (!secfile_lookup_int(file, &x_top_left, "%s.x_top_left", sec_name)
          || !secfile_lookup_int(file, &y_top_left,
                                 "%s.y_top_left", sec_name)
          || !secfile_lookup_int(file, &dx, "%s.dx", sec_name)
          || !secfile_lookup_int(file, &dy, "%s.dy", sec_name)) {
        log_error("Grid \"%s\" invalid: %s", sec_name, secfile_error());
        continue;
      }

      j = -1;
      while (NULL != secfile_entry_lookup(file, "%s.tiles%d.tag",
                                          sec_name, ++j)) {
        struct small_sprite *ss;
        int row, column;
        int x1, y1;
        const char **tags;
        size_t num_tags;
        int hot_x, hot_y;

        if (!secfile_lookup_int(file, &row, "%s.tiles%d.row", sec_name, j)
            || !secfile_lookup_int(file, &column, "%s.tiles%d.column",
                                   sec_name, j)
            || !(tags = secfile_lookup_str_vec(file, &num_tags,
                                               "%s.tiles%d.tag",
                                               sec_name, j))) {
          log_error("Small sprite \"%s.tiles%d\" invalid: %s",
                    sec_name, j, secfile_error());
          continue;
        }
        hot_x = secfile_lookup_int_default(file, 0, "%s.tiles%d.hot_x",
                                           sec_name, j);
        hot_y = secfile_lookup_int_default(file, 0, "%s.tiles%d.hot_y",
                                           sec_name, j);

        /* there must be at least 1 because of the while(): */
        fc_assert_action(num_tags > 0, continue);

        x1 = x_top_left + (dx + pixel_border) * column;
        y1 = y_top_left + (dy + pixel_border) * row;

        ss = fc_malloc(sizeof(*ss));
        ss->ref_count = 0;
        ss->file = NULL;
        ss->x = x1;
        ss->y = y1;
        ss->width = dx;
        ss->height = dy;
        ss->sf = sf;
        ss->sprite = NULL;
        ss->hot_x = hot_x;
        ss->hot_y = hot_y;

        small_sprite_list_prepend(t->small_sprites, ss);

        if (!duplicates_ok) {
          for (k = 0; k < num_tags; k++) {
            if (!sprite_hash_insert(t->sprite_hash, tags[k], ss)) {
              log_error("warning: already have a sprite for \"%s\".",
                        tags[k]);
            }
          }
        } else {
          for (k = 0; k < num_tags; k++) {
            (void) sprite_hash_replace(t->sprite_hash, tags[k], ss);
          }
        }

        free(tags);
        tags = NULL;
      }
    } section_list_iterate_end;
    section_list_destroy(sections);
  }

  /* Load "extra" sprites.  Each sprite is one file. */
  i = -1;
  while (NULL != secfile_entry_lookup(file, "extra.sprites%d.tag", ++i)) {
    struct small_sprite *ss;
    const char **tags;
    const char *filename;
    size_t num_tags, k;
    int hot_x, hot_y;

    if (!(tags = secfile_lookup_str_vec(file, &num_tags,
                                        "extra.sprites%d.tag", i))
        || !(filename = secfile_lookup_str(file,
                                           "extra.sprites%d.file", i))) {
      log_error("Extra sprite \"extra.sprites%d\" invalid: %s",
                i, secfile_error());
      continue;
    }
    hot_x = secfile_lookup_int_default(file, 0, "extra.sprites%d.hot_x", i);
    hot_y = secfile_lookup_int_default(file, 0, "extra.sprites%d.hot_y", i);

    ss = fc_malloc(sizeof(*ss));
    ss->ref_count = 0;
    ss->file = fc_strdup(filename);
    ss->sf = NULL;
    ss->sprite = NULL;
    ss->hot_x = hot_x;
    ss->hot_y = hot_y;

    small_sprite_list_prepend(t->small_sprites, ss);

    if (!duplicates_ok) {
      for (k = 0; k < num_tags; k++) {
        if (!sprite_hash_insert(t->sprite_hash, tags[k], ss)) {
          log_error("warning: already have a sprite for \"%s\".", tags[k]);
        }
      }
    } else {
      for (k = 0; k < num_tags; k++) {
        (void) sprite_hash_replace(t->sprite_hash, tags[k], ss);
      }
    }
    free(tags);
  }

  secfile_check_unused(file);
  secfile_destroy(file);
}

/**********************************************************************
  Returns the correct name of the gfx file (with path and extension)
  Must be free'd when no longer used
***********************************************************************/
static char *tilespec_gfx_filename(const char *gfx_filename)
{
  const char  *gfx_current_fileext;
  const char **gfx_fileexts = gfx_fileextensions();

  while((gfx_current_fileext = *gfx_fileexts++))
  {
    char *full_name =
       fc_malloc(strlen(gfx_filename) + strlen(gfx_current_fileext) + 2);
    const char *real_full_name;

    sprintf(full_name,"%s.%s",gfx_filename,gfx_current_fileext);

    real_full_name = fileinfoname(get_data_dirs(), full_name);
    free(full_name);
    if (real_full_name) {
      return fc_strdup(real_full_name);
    }
  }

  log_fatal("Couldn't find a supported gfx file extension for \"%s\".",
            gfx_filename);
  exit(EXIT_FAILURE);
  return NULL;
}

/**********************************************************************
  Determine the sprite_type string.
***********************************************************************/
static int check_sprite_type(const char *sprite_type, const char *tile_section)
{
  if (fc_strcasecmp(sprite_type, "corner") == 0) {
    return CELL_CORNER;
  }
  if (fc_strcasecmp(sprite_type, "single") == 0) {
    return CELL_WHOLE;
  }
  if (fc_strcasecmp(sprite_type, "whole") == 0) {
    return CELL_WHOLE;
  }
  log_error("[%s] unknown sprite_type \"%s\".", tile_section, sprite_type);
  return CELL_WHOLE;
}

/**********************************************************************
  Finds and reads the toplevel tilespec file based on given name.
  Sets global variables, including tile sizes and full names for
  intro files.
***********************************************************************/
struct tileset *tileset_read_toplevel(const char *tileset_name, bool verbose)
{
  struct section_file *file;
  char *fname;
  const char *c;
  int i;
  size_t num_spec_files;
  const char **spec_filenames;
  struct section_list *sections = NULL;
  const char *file_capstr;
  bool duplicates_ok, is_hex;
  enum direction8 dir;
  const int spl = strlen(TILE_SECTION_PREFIX);
  struct tileset *t = NULL;
  int ei1, ei2;
  const char *roadname;
  const char *tstr;

  fname = tilespec_fullname(tileset_name);
  if (!fname) {
    if (verbose) {
      log_error("Can't find tileset \"%s\".", tileset_name); 
    }
    return NULL;
  }
  log_verbose("tilespec file is \"%s\".", fname);

  if (!(file = secfile_load(fname, TRUE))) {
    log_error("Could not open '%s':\n%s", fname, secfile_error());
    free(fname);
    return NULL;
  }

  if (!check_tilespec_capabilities(file, "tilespec",
                                   TILESPEC_CAPSTR, fname, verbose)) {
    secfile_destroy(file);
    free(fname);
    return NULL;
  }

  t = tileset_new();
  file_capstr = secfile_lookup_str(file, "%s.options", "tilespec");
  duplicates_ok = (NULL != file_capstr
                   && has_capabilities("+duplicates_ok", file_capstr));

  (void) secfile_entry_lookup(file, "tilespec.name"); /* currently unused */

  sz_strlcpy(t->name, tileset_name);
  if (!secfile_lookup_int(file, &t->priority, "tilespec.priority")
      || !secfile_lookup_bool(file, &is_hex, "tilespec.is_hex")) {
    log_error("Tileset \"%s\" invalid: %s", t->name, secfile_error());
    goto ON_ERROR;
  }

  tstr = secfile_lookup_str(file, "tilespec.type");
  if (tstr == NULL) {
    log_error("Tileset \"%s\": no tileset type", t->name);
    goto ON_ERROR;
  }

  if (!fc_strcasecmp(tstr, "overview")) {
    t->type = TS_OVERVIEW;
  } else if (!fc_strcasecmp(tstr, "isometric")) {
    t->type = TS_ISOMETRIC;
  } else {
    log_error("Tileset \"%s\": unknown tileset type \"%s\"", t->name, tstr);
    goto ON_ERROR;
  }

  /* Read hex-tileset information. */
  t->hex_width = t->hex_height = 0;
  if (is_hex) {
    int hex_side;

    if (!secfile_lookup_int(file, &hex_side, "tilespec.hex_side")) {
      log_error("Tileset \"%s\" invalid: %s", t->name, secfile_error());
      goto ON_ERROR;
    }

    if (t->type == TS_ISOMETRIC) {
      t->hex_height = hex_side;
    } else {
      t->hex_width = hex_side;
    }
    /* Hex tilesets are drawn the same as isometric. */
    /* FIXME: There will be other legal values to be used with hex
     * tileset in the future, and this would just overwrite it. */
    t->type = TS_ISOMETRIC;
  }

  if (t->type == TS_ISOMETRIC && !isometric_view_supported()) {
    log_normal(_("Client does not support isometric tilesets."));
    log_normal(_("Using default tileset instead."));
    fc_assert(tileset_name != NULL);
    goto ON_ERROR;
  }
  if (t->type == TS_OVERVIEW && !overhead_view_supported()) {
    log_normal(_("Client does not support overhead view tilesets."));
    log_normal(_("Using default tileset instead."));
    fc_assert(tileset_name != NULL);
    goto ON_ERROR;
  }

  /* Create arrays of valid and cardinal tileset dirs.  These depend
   * entirely on the tileset, not the topology.  They are also in clockwise
   * rotational ordering. */
  t->num_valid_tileset_dirs = t->num_cardinal_tileset_dirs = 0;
  dir = DIR8_NORTH;
  do {
    if (is_valid_tileset_dir(t, dir)) {
      t->valid_tileset_dirs[t->num_valid_tileset_dirs] = dir;
      t->num_valid_tileset_dirs++;
    }
    if (is_cardinal_tileset_dir(t, dir)) {
      t->cardinal_tileset_dirs[t->num_cardinal_tileset_dirs] = dir;
      t->num_cardinal_tileset_dirs++;
    }

    dir = dir_cw(dir);
  } while (dir != DIR8_NORTH);
  fc_assert(t->num_valid_tileset_dirs % 2 == 0); /* Assumed elsewhere. */
  t->num_index_valid = 1 << t->num_valid_tileset_dirs;
  t->num_index_cardinal = 1 << t->num_cardinal_tileset_dirs;

  if (!secfile_lookup_int(file, &t->normal_tile_width,
                          "tilespec.normal_tile_width")
      || !secfile_lookup_int(file, &t->normal_tile_height,
                             "tilespec.normal_tile_height")) {
    log_error("Tileset \"%s\" invalid: %s", t->name, secfile_error());
    goto ON_ERROR;
  }
  if (t->type == TS_ISOMETRIC) {
    t->full_tile_width = t->normal_tile_width;
    t->full_tile_height = 3 * t->normal_tile_height / 2;
  } else {
    t->full_tile_width = t->normal_tile_width;
    t->full_tile_height = t->normal_tile_height;
  }
  t->unit_tile_width
    = secfile_lookup_int_default(file, t->full_tile_width, "tilespec.unit_width");
  t->unit_tile_height
    = secfile_lookup_int_default(file, t->full_tile_height, "tilespec.unit_height");
  if (!secfile_lookup_int(file, &t->small_sprite_width,
                          "tilespec.small_tile_width")
      || !secfile_lookup_int(file, &t->small_sprite_height,
                             "tilespec.small_tile_height")) {
    log_error("Tileset \"%s\" invalid: %s", t->name, secfile_error());
    goto ON_ERROR;
  }
  log_verbose("tile sizes %dx%d, %d%d unit, %d%d small",
              t->normal_tile_width, t->normal_tile_height,
              t->full_tile_width, t->full_tile_height,
              t->small_sprite_width, t->small_sprite_height);

  /* FIXME: use specenum to load these. */
  if (!secfile_lookup_int(file, &ei1,
                          "tilespec.fogstyle")
      || !secfile_lookup_int(file, &ei2,
                             "tilespec.darkness_style")) {
    log_error("Tileset \"%s\" invalid: %s", t->name, secfile_error());
    goto ON_ERROR;
  }
  t->fogstyle = ei1;
  t->darkness_style = ei2;

  if (t->darkness_style < DARKNESS_NONE
      || t->darkness_style > DARKNESS_CORNER
      || (t->darkness_style == DARKNESS_ISORECT
          && (t->type == TS_OVERVIEW || t->hex_width > 0 || t->hex_height > 0))) {
    log_error("Invalid darkness style set in tileset \"%s\".", t->name);
    goto ON_ERROR;
  }
  if (!secfile_lookup_int(file, &t->unit_flag_offset_x,
                          "tilespec.unit_flag_offset_x")
      || !secfile_lookup_int(file, &t->unit_flag_offset_y,
                             "tilespec.unit_flag_offset_y")
      || !secfile_lookup_int(file, &t->city_flag_offset_x,
                             "tilespec.city_flag_offset_x")
      || !secfile_lookup_int(file, &t->city_flag_offset_y,
                             "tilespec.city_flag_offset_y")
      || !secfile_lookup_int(file, &t->unit_offset_x,
                             "tilespec.unit_offset_x")
      || !secfile_lookup_int(file, &t->unit_offset_y,
                             "tilespec.unit_offset_y")
      || !secfile_lookup_int(file, &t->citybar_offset_y,
                             "tilespec.citybar_offset_y")
      || !secfile_lookup_int(file, &t->tilelabel_offset_y,
                             "tilespec.tilelabel_offset_y")
      || !secfile_lookup_int(file, &t->city_names_font_size,
                             "tilespec.city_names_font_size")
      || !secfile_lookup_int(file, &t->city_productions_font_size,
                             "tilespec.city_productions_font_size")) {
    log_error("Tileset \"%s\" invalid: %s", t->name, secfile_error());
    goto ON_ERROR;
  }

  set_city_names_font_sizes(t->city_names_font_size,
                            t->city_productions_font_size);

  c = secfile_lookup_str(file, "tilespec.main_intro_file");
  t->main_intro_filename = tilespec_gfx_filename(c);
  log_debug("intro file %s", t->main_intro_filename);

  c = secfile_lookup_str(file, "tilespec.minimap_intro_file");
  t->minimap_intro_filename = tilespec_gfx_filename(c);
  log_debug("radar file %s", t->minimap_intro_filename);

  /* Terrain layer info. */
  for (i = 0; i < MAX_NUM_LAYERS; i++) {
    struct tileset_layer *tslp = &t->layers[i];
    int j, k;

    tslp->match_types =
        (char **) secfile_lookup_str_vec(file, &tslp->match_count,
                                         "layer%d.match_types", i);
    for (j = 0; j < tslp->match_count; j++) {
      tslp->match_types[j] = fc_strdup(tslp->match_types[j]);

      for (k = 0; k < j; k++) {
        if (tslp->match_types[k][0] == tslp->match_types[j][0]) {
          log_fatal("[layer%d] match_types: \"%s\" initial "
                    "('%c') is not unique.",
                    i, tslp->match_types[j], tslp->match_types[j][0]);
          exit(EXIT_FAILURE); /* FIXME: Returns NULL. */
        }
      }
    }
  }

  /* Tile drawing info. */
  sections = secfile_sections_by_name_prefix(file, TILE_SECTION_PREFIX);
  if (NULL == sections || 0 == section_list_size(sections)) {
    log_error("No [%s] sections supported by tileset \"%s\".",
              TILE_SECTION_PREFIX, fname);
    goto ON_ERROR;
  }

  fc_assert(t->tile_hash == NULL);
  t->tile_hash = drawing_hash_new();

  section_list_iterate(sections, psection) {
    const char *sec_name = section_name(psection);
    struct drawing_data *draw = drawing_data_new(sec_name + spl);
    const char *sprite_type, *str;
    int l;

    draw->blending = secfile_lookup_int_default(file, 0, "%s.is_blended",
                                                sec_name);
    draw->blending = CLIP(0, draw->blending, MAX_NUM_LAYERS);

    draw->is_reversed = secfile_lookup_bool_default(file, FALSE,
                                                    "%s.is_reversed",
                                                    sec_name);
    draw->num_layers = secfile_lookup_int_default(file, 0, "%s.num_layers",
                                                  sec_name);
    draw->num_layers = CLIP(1, draw->num_layers, MAX_NUM_LAYERS);

    for (l = 0; l < draw->num_layers; l++) {
      struct drawing_layer *dlp = &draw->layer[l];
      struct tileset_layer *tslp = &t->layers[l];
      const char *match_type;
      const char **match_with;
      size_t count;

      dlp->is_tall
        = secfile_lookup_bool_default(file, FALSE, "%s.layer%d_is_tall",
                                      sec_name, l);
      dlp->offset_x
        = secfile_lookup_int_default(file, 0, "%s.layer%d_offset_x",
                                     sec_name, l);
      dlp->offset_y
        = secfile_lookup_int_default(file, 0, "%s.layer%d_offset_y",
                                     sec_name, l);

      match_type = secfile_lookup_str_default(file, NULL,
                                              "%s.layer%d_match_type",
                                              sec_name, l);
      if (match_type) {
        int j;

        /* Determine our match_type. */
        for (j = 0; j < tslp->match_count; j++) {
          if (fc_strcasecmp(tslp->match_types[j], match_type) == 0) {
            break;
          }
        }
        if (j >= tslp->match_count) {
          log_error("[%s] invalid match_type \"%s\".", sec_name, match_type);
        } else {
          dlp->match_index[dlp->match_indices++] = j;
        }
      }

      match_with = secfile_lookup_str_vec(file, &count,
                                          "%s.layer%d_match_with",
                                          sec_name, l);
      if (match_with) {
        int j, k;

        if (count > MAX_NUM_MATCH_WITH) {
          log_error("[%s] match_with has too many types (%d, max %d)",
                    sec_name, (int) count, MAX_NUM_MATCH_WITH);
          count = MAX_NUM_MATCH_WITH;
        }

        if (1 < dlp->match_indices) {
          log_error("[%s] previous match_with ignored.", sec_name);
          dlp->match_indices = 1;
        } else if (1 > dlp->match_indices) {
          log_error("[%s] missing match_type, using \"%s\".",
                    sec_name, tslp->match_types[0]);
          dlp->match_index[0] = 0;
          dlp->match_indices = 1;
        }

        for (k = 0; k < count; k++) {
          for (j = 0; j < tslp->match_count; j++) {
            if (fc_strcasecmp(tslp->match_types[j], match_with[k]) == 0) {
              break;
            }
          }
          if (j >= tslp->match_count) {
            log_error("[%s] layer%d_match_with: invalid  \"%s\".",
                      sec_name, l, match_with[k]);
          } else if (1 < count) {
            int m;

            for (m = 0; m < dlp->match_indices; m++) {
              if (dlp->match_index[m] == j) {
                log_error("[%s] layer%d_match_with: duplicate \"%s\".",
                          sec_name, l, match_with[k]);
                break;
              }
            }
            if (m >= dlp->match_indices) {
              dlp->match_index[dlp->match_indices++] = j;
            }
          } else {
            dlp->match_index[dlp->match_indices++] = j;
          }
        }
        free(match_with);
        match_with = NULL;
      }

      /* Check match_indices */
      switch (dlp->match_indices) {
      case 0:
      case 1:
        dlp->match_style = MATCH_NONE;
        break;
      case 2:
        if (dlp->match_index[0] == dlp->match_index[1] ) {
          dlp->match_style = MATCH_SAME;
        } else {
          dlp->match_style = MATCH_PAIR;
        }
        break;
      default:
        dlp->match_style = MATCH_FULL;
        break;
      };

      sprite_type
	= secfile_lookup_str_default(file, "whole", "%s.layer%d_sprite_type",
                                     sec_name, l);
      dlp->sprite_type = check_sprite_type(sprite_type, sec_name);

      switch (dlp->sprite_type) {
      case CELL_WHOLE:
	/* OK, no problem */
	break;
      case CELL_CORNER:
	if (dlp->is_tall
	    || dlp->offset_x > 0
	    || dlp->offset_y > 0) {
          log_error("[%s] layer %d: you cannot have tall terrain or\n"
                    "a sprite offset with a cell-based drawing method.",
                    sec_name, l);
	  dlp->is_tall = FALSE;
	  dlp->offset_x = dlp->offset_y = 0;
	}
	break;
      };
    }

    str = secfile_lookup_str(file, "%s.mine_sprite", sec_name);
    if (NULL != str) {
      draw->mine_tag = fc_strdup(str);
    }

    if (!drawing_hash_insert(t->tile_hash, draw->name, draw)) {
      log_error("warning: duplicate tilespec entry [%s].", sec_name);
      goto ON_ERROR;
    }
  } section_list_iterate_end;
  section_list_destroy(sections);
  sections = NULL;

  t->rstyle_hash = rstyle_hash_new();
  t->rivers = road_type_list_new();

  for (i = 0; (roadname = secfile_lookup_str_default(file, NULL,
                                                     "roads.styles%d.name",
                                                     i)); i++) {
    const char *style_name;
    enum roadstyle_id *style = fc_malloc(sizeof(enum roadstyle_id));
    char *name;

    style_name = secfile_lookup_str_default(file, "AllSeparate",
                                            "roads.styles%d.style", i);
    *style = roadstyle_id_by_name(style_name, fc_strcasecmp);
    if (!roadstyle_id_is_valid(*style)) {
      log_error("Unknown road style \"%s\" for road \"%s\"",
                style_name, roadname);
      FC_FREE(style);
      goto ON_ERROR;
    }

    name = fc_malloc(strlen(roadname) + 1);
    strcpy(name, roadname);

    if (!rstyle_hash_insert(t->rstyle_hash, name, style)) {
      log_error("warning: duplicate roadstyle entry [%s].", roadname);
      goto ON_ERROR;
    }
  }

  spec_filenames = secfile_lookup_str_vec(file, &num_spec_files,
                                          "tilespec.files");
  if (NULL == spec_filenames || 0 == num_spec_files) {
    log_error("No tile graphics files specified in \"%s\"", fname);
    goto ON_ERROR;
  }

  fc_assert(t->sprite_hash == NULL);
  t->sprite_hash = sprite_hash_new();
  for (i = 0; i < num_spec_files; i++) {
    struct specfile *sf = fc_malloc(sizeof(*sf));
    const char *dname;

    log_debug("spec file %s", spec_filenames[i]);
    
    sf->big_sprite = NULL;
    dname = fileinfoname(get_data_dirs(), spec_filenames[i]);
    if (!dname) {
      if (verbose) {
        log_error("Can't find spec file \"%s\".", spec_filenames[i]);
      }
      goto ON_ERROR;
    }
    sf->file_name = fc_strdup(dname);
    scan_specfile(t, sf, duplicates_ok);

    specfile_list_prepend(t->specfiles, sf);
  }
  free(spec_filenames);

  t->color_system = color_system_read(file);

  /* FIXME: remove this hack. */
  t->prefered_themes =
    (char **) secfile_lookup_str_vec(file, (size_t *)
                                     &t->num_prefered_themes,
                                     "tilespec.preferred_themes");
  if (t->num_prefered_themes <= 0) {
    t->prefered_themes =
      (char **) secfile_lookup_str_vec(file, (size_t *)
                                       &t->num_prefered_themes,
                                       "tilespec.prefered_themes");
  }
  for (i = 0; i < t->num_prefered_themes; i++) {
    t->prefered_themes[i] = fc_strdup(t->prefered_themes[i]);
  }

  secfile_check_unused(file);
  secfile_destroy(file);
  log_verbose("finished reading \"%s\".", fname);
  free(fname);

  return t;

ON_ERROR:
  secfile_destroy(file);
  free(fname);
  tileset_free(t);
  if (NULL != sections) {
    section_list_destroy(sections);
  }
  return NULL;
}

/**********************************************************************
  Returns a text name for the citizen, as used in the tileset.
***********************************************************************/
static const char *citizen_rule_name(enum citizen_category citizen)
{
  /* These strings are used in reading the tileset.  Do not
   * translate. */
  switch (citizen) {
  case CITIZEN_HAPPY:
    return "happy";
  case CITIZEN_CONTENT:
    return "content";
  case CITIZEN_UNHAPPY:
    return "unhappy";
  case CITIZEN_ANGRY:
    return "angry";
  default:
    break;
  }
  log_error("Unknown citizen type: %d.", (int) citizen);
  return NULL;
}

/****************************************************************************
  Return a directional string for the cardinal directions.  Normally the
  binary value 1000 will be converted into "n1e0s0w0".  This is in a
  clockwise ordering.
****************************************************************************/
static const char *cardinal_index_str(const struct tileset *t, int idx)
{
  static char c[64];
  int i;

  c[0] = '\0';
  for (i = 0; i < t->num_cardinal_tileset_dirs; i++) {
    int value = (idx >> i) & 1;

    cat_snprintf(c, sizeof(c), "%s%d",
		 dir_get_tileset_name(t->cardinal_tileset_dirs[i]), value);
  }

  return c;
}

/****************************************************************************
  Do the same thing as cardinal_str, except including all valid directions.
  The returned string is a pointer to static memory.
****************************************************************************/
static char *valid_index_str(const struct tileset *t, int index)
{
  static char c[64];
  int i;

  c[0] = '\0';
  for (i = 0; i < t->num_valid_tileset_dirs; i++) {
    int value = (index >> i) & 1;

    cat_snprintf(c, sizeof(c), "%s%d",
		 dir_get_tileset_name(t->valid_tileset_dirs[i]), value);
  }

  return c;
}

/**************************************************************************
  Loads the sprite. If the sprite is already loaded a reference
  counter is increased. Can return NULL if the sprite couldn't be
  loaded.
**************************************************************************/
static struct sprite *load_sprite(struct tileset *t, const char *tag_name)
{
  struct small_sprite *ss;

  log_debug("load_sprite(tag='%s')", tag_name);
  /* Lookup information about where the sprite is found. */
  if (!sprite_hash_lookup(t->sprite_hash, tag_name, &ss)) {
    return NULL;
  }

  fc_assert(ss->ref_count >= 0);

  if (!ss->sprite) {
    /* If the sprite hasn't been loaded already, then load it. */
    fc_assert(ss->ref_count == 0);
    if (ss->file) {
      ss->sprite = load_gfx_file(ss->file);
      if (!ss->sprite) {
        log_fatal("Couldn't load gfx file \"%s\" for sprite '%s'.",
                  ss->file, tag_name);
        exit(EXIT_FAILURE);
      }
    } else {
      int sf_w, sf_h;

      ensure_big_sprite(ss->sf);
      get_sprite_dimensions(ss->sf->big_sprite, &sf_w, &sf_h);
      if (ss->x < 0 || ss->x + ss->width > sf_w
	  || ss->y < 0 || ss->y + ss->height > sf_h) {
        log_error("Sprite '%s' in file \"%s\" isn't within the image!",
                  tag_name, ss->sf->file_name);
	return NULL;
      }
      ss->sprite =
	crop_sprite(ss->sf->big_sprite, ss->x, ss->y, ss->width, ss->height,
		    NULL, -1, -1);
    }
  }

  /* Track the reference count so we know when to free the sprite. */
  ss->ref_count++;

  return ss->sprite;
}

/**************************************************************************
  Create a sprite with the given color and tag.
**************************************************************************/
static struct sprite *create_plr_sprite(struct color *pcolor)
{
  struct sprite *sprite;

  fc_assert_ret_val(pcolor != NULL, NULL);

  sprite = create_sprite(128, 64, pcolor);

  return sprite;
}

/**************************************************************************
  Unloads the sprite. Decrease the reference counter. If the last
  reference is removed the sprite is freed.
**************************************************************************/
static void unload_sprite(struct tileset *t, const char *tag_name)
{
  struct small_sprite *ss;

  sprite_hash_lookup(t->sprite_hash, tag_name, &ss);
  fc_assert_ret(ss);
  fc_assert_ret(ss->ref_count >= 1);
  fc_assert_ret(ss->sprite);

  ss->ref_count--;

  if (ss->ref_count == 0) {
    /* Nobody's using the sprite anymore, so we should free it.  We know
     * where to find it if we need it again. */
    log_debug("freeing sprite '%s'.", tag_name);
    free_sprite(ss->sprite);
    ss->sprite = NULL;
  }
}

/**************************************************************************
  Return TRUE iff the specified sprite exists in the tileset (whether
  or not it is currently loaded).
**************************************************************************/
static bool sprite_exists(const struct tileset *t, const char *tag_name)
{
  /* Lookup information about where the sprite is found. */
  return sprite_hash_lookup(t->sprite_hash, tag_name, NULL);
}

/* Not very safe, but convenient: */
#define SET_SPRITE(field, tag)					  \
  do {								  \
    t->sprites.field = load_sprite(t, tag);			  \
    fc_assert_exit_msg(NULL != t->sprites.field,                  \
                       "Sprite tag '%s' missing.", tag);          \
  } while(FALSE)

/* Sets sprites.field to tag or (if tag isn't available) to alt */
#define SET_SPRITE_ALT(field, tag, alt)					    \
  do {									    \
    t->sprites.field = load_sprite(t, tag);				    \
    if (!t->sprites.field) {						    \
      t->sprites.field = load_sprite(t, alt);				    \
    }									    \
    fc_assert_exit_msg(NULL != t->sprites.field,                            \
                       "Sprite tag '%s' and alternate '%s' are "            \
                       "both missing.", tag, alt)                           \
  } while(FALSE)

/* Sets sprites.field to tag, or NULL if not available */
#define SET_SPRITE_OPT(field, tag) \
  t->sprites.field = load_sprite(t, tag)

#define SET_SPRITE_ALT_OPT(field, tag, alt)				    \
  do {									    \
    t->sprites.field = tiles_lookup_sprite_tag_alt(t, LOG_VERBOSE, tag, alt,\
						   "sprite", #field);	    \
  } while (FALSE)

/****************************************************************************
  Setup the graphics for specialist types.
****************************************************************************/
void tileset_setup_specialist_type(struct tileset *t, Specialist_type_id id)
{
  /* Load the specialist sprite graphics. */
  char buffer[512];
  int j;
  const char *name = specialist_rule_name(specialist_by_number(id));

  for (j = 0; j < MAX_NUM_CITIZEN_SPRITES; j++) {
    fc_snprintf(buffer, sizeof(buffer), "specialist.%s_%d", name, j);
    t->sprites.specialist[id].sprite[j] = load_sprite(t, buffer);
    if (!t->sprites.specialist[id].sprite[j]) {
      break;
    }
  }
  t->sprites.specialist[id].count = j;
  if (j == 0) {
    log_fatal("No graphics for specialist \"%s\".", name);
    exit(EXIT_FAILURE);
  }
}

/****************************************************************************
  Setup the graphics for (non-specialist) citizen types.
****************************************************************************/
static void tileset_setup_citizen_types(struct tileset *t)
{
  int i, j;
  char buffer[512];

  /* Load the citizen sprite graphics, no specialist. */
  for (i = 0; i < CITIZEN_LAST; i++) {
    const char *name = citizen_rule_name(i);

    for (j = 0; j < MAX_NUM_CITIZEN_SPRITES; j++) {
      fc_snprintf(buffer, sizeof(buffer), "citizen.%s_%d", name, j);
      t->sprites.citizen[i].sprite[j] = load_sprite(t, buffer);
      if (!t->sprites.citizen[i].sprite[j]) {
	break;
      }
    }
    t->sprites.citizen[i].count = j;
    if (j == 0) {
      log_fatal("No graphics for citizen \"%s\".", name);
      exit(EXIT_FAILURE);
    }
  }
}

/****************************************************************************
  Return the sprite in the city_sprite listing that corresponds to this
  city - based on city style and size.

  See also load_city_sprite, free_city_sprite.
****************************************************************************/
static struct sprite *get_city_sprite(const struct city_sprite *city_sprite,
				      const struct city *pcity)
{
  /* get style and match the best tile based on city size */
  int style = style_of_city(pcity);
  int num_thresholds;
  struct city_style_threshold *thresholds;
  int img_index;

  fc_assert_ret_val(style < city_sprite->num_styles, NULL);

  if (is_ocean_tile(pcity->tile)
      && city_sprite->styles[style].oceanic_num_thresholds != 0) {
    num_thresholds = city_sprite->styles[style].oceanic_num_thresholds;
    thresholds = city_sprite->styles[style].oceanic_thresholds;
  } else {
    num_thresholds = city_sprite->styles[style].land_num_thresholds;
    thresholds = city_sprite->styles[style].land_thresholds;
  }

  if (num_thresholds == 0) {
    return NULL;
  }

  /* Get the sprite with the index defined by the effects. */
  img_index = pcity->client.city_image;
  if (img_index == -100) {
    /* Server doesn't know right value as this is from old savegame.
     * Guess here based on *client* side information as was done in
     * versions where information was not saved to savegame - this should
     * give us right answer of what city looked like by the time it was
     * put under FoW. */
    img_index = get_city_bonus(pcity, EFT_CITY_IMAGE);
  }
  img_index = CLIP(0, img_index, num_thresholds - 1);

  return thresholds[img_index].sprite;
}

/****************************************************************************
  Allocates one threshold set for city sprite
****************************************************************************/
static int load_city_thresholds_sprites(struct tileset *t, const char *tag,
                                        char *graphic, char *graphic_alt,
                                        struct city_style_threshold **thresholds)
{
  char buffer[128];
  char *gfx_in_use = graphic;
  int num_thresholds = 0;
  struct sprite *sprite;
  int size;

  *thresholds = NULL;

  for (size = 0; size < MAX_CITY_SIZE; size++) {
    fc_snprintf(buffer, sizeof(buffer), "%s_%s_%d",
                gfx_in_use, tag, size);
    if ((sprite = load_sprite(t, buffer))) {
      num_thresholds++;
      *thresholds = fc_realloc(*thresholds, num_thresholds * sizeof(**thresholds));
      (*thresholds)[num_thresholds - 1].sprite = sprite;
    } else if (size == 0) {
      if (gfx_in_use == graphic) {
        /* Try again with graphic_alt. */
        size--;
        gfx_in_use = graphic_alt;
      } else {
        /* Don't load any others if the 0 element isn't there. */
        break;
      }
    }
  }

  return num_thresholds;
}

/****************************************************************************
  Allocates and loads a new city sprite from the given sprite tags.

  tag may be NULL.

  See also get_city_sprite, free_city_sprite.
****************************************************************************/
static struct city_sprite *load_city_sprite(struct tileset *t,
					    const char *tag)
{
  struct city_sprite *city_sprite = fc_malloc(sizeof(*city_sprite));
  int style;

  /* Store number of styles we have allocated memory for.
   * game.control.styles_count might change if client disconnects from
   * server and connects new one. */
  city_sprite->num_styles = game.control.styles_count;
  city_sprite->styles = fc_malloc(city_sprite->num_styles
				  * sizeof(*city_sprite->styles));

  for (style = 0; style < city_sprite->num_styles; style++) {
    city_sprite->styles[style].land_num_thresholds =
      load_city_thresholds_sprites(t, tag, city_styles[style].graphic,
                                   city_styles[style].graphic_alt,
                                   &city_sprite->styles[style].land_thresholds);
    city_sprite->styles[style].oceanic_num_thresholds =
      load_city_thresholds_sprites(t, tag, city_styles[style].oceanic_graphic,
                                   city_styles[style].oceanic_graphic_alt,
                                   &city_sprite->styles[style].oceanic_thresholds);
  }

  return city_sprite;
}

/****************************************************************************
  Frees a city sprite.

  See also get_city_sprite, load_city_sprite.
****************************************************************************/
static void free_city_sprite(struct city_sprite *city_sprite)
{
  int style;

  if (!city_sprite) {
    return;
  }
  for (style = 0; style < city_sprite->num_styles; style++) {
    if (city_sprite->styles[style].land_thresholds) {
      free(city_sprite->styles[style].land_thresholds);
    }
    if (city_sprite->styles[style].oceanic_thresholds) {
      free(city_sprite->styles[style].oceanic_thresholds);
    }
  }
  free(city_sprite->styles);
  free(city_sprite);
}

/**********************************************************************
  Initialize 'sprites' structure based on hardwired tags which
  freeciv always requires. 
***********************************************************************/
static void tileset_lookup_sprite_tags(struct tileset *t)
{
  char buffer[512], buffer2[512];
  const int W = t->normal_tile_width, H = t->normal_tile_height;
  int i, j, f;

  fc_assert_ret(t->sprite_hash != NULL);

  SET_SPRITE(treaty_thumb[0], "treaty.disagree_thumb_down");
  SET_SPRITE(treaty_thumb[1], "treaty.agree_thumb_up");

  for (j = 0; j < INDICATOR_COUNT; j++) {
    const char *names[] = {"science_bulb", "warming_sun", "cooling_flake"};

    for (i = 0; i < NUM_TILES_PROGRESS; i++) {
      fc_snprintf(buffer, sizeof(buffer), "s.%s_%d", names[j], i);
      SET_SPRITE(indicator[j][i], buffer);
    }
  }

  SET_SPRITE(arrow[ARROW_RIGHT], "s.right_arrow");
  SET_SPRITE(arrow[ARROW_PLUS], "s.plus");
  SET_SPRITE(arrow[ARROW_MINUS], "s.minus");
  if (t->type == TS_ISOMETRIC) {
    SET_SPRITE(dither_tile, "t.dither_tile");
  }

  SET_SPRITE(mask.tile, "mask.tile");
  SET_SPRITE(mask.worked_tile, "mask.worked_tile");
  SET_SPRITE(mask.unworked_tile, "mask.unworked_tile");

  SET_SPRITE(tax_luxury, "s.tax_luxury");
  SET_SPRITE(tax_science, "s.tax_science");
  SET_SPRITE(tax_gold, "s.tax_gold");

  tileset_setup_citizen_types(t);

  for (i = 0; i < SPACESHIP_COUNT; i++) {
    const char *names[SPACESHIP_COUNT]
      = {"solar_panels", "life_support", "habitation",
	 "structural", "fuel", "propulsion", "exhaust"};

    fc_snprintf(buffer, sizeof(buffer), "spaceship.%s", names[i]);
    SET_SPRITE(spaceship[i], buffer);
  }

  for (i = 0; i < CURSOR_LAST; i++) {
    for (f = 0; f < NUM_CURSOR_FRAMES; f++) {
      const char *names[CURSOR_LAST] =
               {"goto", "patrol", "paradrop", "nuke", "select", 
		"invalid", "attack", "edit_paint", "edit_add", "wait"};
      struct small_sprite *ss;

      fc_assert(ARRAY_SIZE(names) == CURSOR_LAST);
      fc_snprintf(buffer, sizeof(buffer), "cursor.%s%d", names[i], f);
      SET_SPRITE(cursor[i].frame[f], buffer);
      if (sprite_hash_lookup(t->sprite_hash, buffer, &ss)) {
        t->sprites.cursor[i].hot_x = ss->hot_x;
        t->sprites.cursor[i].hot_y = ss->hot_y;
      }
    }
  }

  for (i = 0; i < ICON_COUNT; i++) {
    const char *names[ICON_COUNT] = {"freeciv", "citydlg"};

    fc_snprintf(buffer, sizeof(buffer), "icon.%s", names[i]);
    SET_SPRITE(icon[i], buffer);
  }

  SET_SPRITE(explode.nuke, "explode.nuke");

  sprite_vector_init(&t->sprites.explode.unit);
  for (i = 0; ; i++) {
    struct sprite *sprite;

    fc_snprintf(buffer, sizeof(buffer), "explode.unit_%d", i);
    sprite = load_sprite(t, buffer);
    if (!sprite) {
      break;
    }
    sprite_vector_append(&t->sprites.explode.unit, sprite);
  }

  SET_SPRITE(unit.auto_attack,  "unit.auto_attack");
  SET_SPRITE(unit.auto_settler, "unit.auto_settler");
  SET_SPRITE(unit.auto_explore, "unit.auto_explore");
  SET_SPRITE(unit.fallout,	"unit.fallout");
  SET_SPRITE(unit.fortified,	"unit.fortified");     
  SET_SPRITE(unit.fortifying,	"unit.fortifying");     
  SET_SPRITE(unit.go_to,	"unit.goto");     
  SET_SPRITE(unit.irrigate,     "unit.irrigate");
  SET_SPRITE(unit.mine,	        "unit.mine");
  SET_SPRITE(unit.pillage,	"unit.pillage");
  SET_SPRITE(unit.pollution,    "unit.pollution");
  SET_SPRITE(unit.sentry,	"unit.sentry");
  SET_SPRITE(unit.convert,      "unit.convert");      
  SET_SPRITE(unit.stack,	"unit.stack");
  SET_SPRITE(unit.loaded,       "unit.loaded");
  SET_SPRITE(unit.transform,    "unit.transform");
  SET_SPRITE(unit.connect,      "unit.connect");
  SET_SPRITE(unit.patrol,       "unit.patrol");
  for (i = 0; i < MAX_NUM_BATTLEGROUPS; i++) {
    fc_snprintf(buffer, sizeof(buffer), "unit.battlegroup_%d", i);
    fc_snprintf(buffer2, sizeof(buffer2), "city.size_%d", i + 1);
    fc_assert(MAX_NUM_BATTLEGROUPS < NUM_TILES_DIGITS);
    SET_SPRITE_ALT(unit.battlegroup[i], buffer, buffer2);
  }
  SET_SPRITE(unit.lowfuel, "unit.lowfuel");
  SET_SPRITE(unit.tired, "unit.tired");

  for(i=0; i<NUM_TILES_HP_BAR; i++) {
    fc_snprintf(buffer, sizeof(buffer), "unit.hp_%d", i*10);
    SET_SPRITE(unit.hp_bar[i], buffer);
  }

  for (i = 0; i < MAX_VET_LEVELS; i++) {
    /* Veteran level sprites are optional.  For instance "green" units
     * usually have no special graphic. */
    fc_snprintf(buffer, sizeof(buffer), "unit.vet_%d", i);
    t->sprites.unit.vet_lev[i] = load_sprite(t, buffer);
  }

  t->sprites.unit.select[0] = NULL;
  if (sprite_exists(t, "unit.select0")) {
    for (i = 0; i < NUM_TILES_SELECT; i++) {
      fc_snprintf(buffer, sizeof(buffer), "unit.select%d", i);
      SET_SPRITE(unit.select[i], buffer);
    }
  }

  SET_SPRITE(citybar.shields, "citybar.shields");
  SET_SPRITE(citybar.food, "citybar.food");
  SET_SPRITE(citybar.trade, "citybar.trade");
  SET_SPRITE(citybar.occupied, "citybar.occupied");
  SET_SPRITE(citybar.background, "citybar.background");
  sprite_vector_init(&t->sprites.citybar.occupancy);
  for (i = 0; ; i++) {
    struct sprite *sprite;

    fc_snprintf(buffer, sizeof(buffer), "citybar.occupancy_%d", i);
    sprite = load_sprite(t, buffer);
    if (!sprite) {
      break;
    }
    sprite_vector_append(&t->sprites.citybar.occupancy, sprite);
  }
  if (t->sprites.citybar.occupancy.size < 2) {
    log_fatal("Missing necessary citybar.occupancy_N sprites.");
    exit(EXIT_FAILURE);
  }

#define SET_EDITOR_SPRITE(x) SET_SPRITE(editor.x, "editor." #x)
  SET_EDITOR_SPRITE(erase);
  SET_EDITOR_SPRITE(brush);
  SET_EDITOR_SPRITE(copy);
  SET_EDITOR_SPRITE(paste);
  SET_EDITOR_SPRITE(copypaste);
  SET_EDITOR_SPRITE(startpos);
  SET_EDITOR_SPRITE(terrain);
  SET_EDITOR_SPRITE(terrain_resource);
  SET_EDITOR_SPRITE(terrain_special);
  SET_EDITOR_SPRITE(unit);
  SET_EDITOR_SPRITE(city);
  SET_EDITOR_SPRITE(vision);
  SET_EDITOR_SPRITE(territory);
  SET_EDITOR_SPRITE(properties);
  SET_EDITOR_SPRITE(road);
  SET_EDITOR_SPRITE(military_base);
#undef SET_EDITOR_SPRITE

  SET_SPRITE(city.disorder, "city.disorder");

  for(i=0; i<NUM_TILES_DIGITS; i++) {
    fc_snprintf(buffer, sizeof(buffer), "city.size_%d", i);
    SET_SPRITE(city.size[i], buffer);
    fc_snprintf(buffer2, sizeof(buffer2), "path.turns_%d", i);
    SET_SPRITE_ALT(path.turns[i], buffer2, buffer);

    fc_snprintf(buffer, sizeof(buffer), "city.size_%d0", i);
    SET_SPRITE(city.size_tens[i], buffer);
    fc_snprintf(buffer2, sizeof(buffer2), "path.turns_%d0", i);
    SET_SPRITE_ALT(path.turns_tens[i], buffer2, buffer);

    fc_snprintf(buffer, sizeof(buffer), "city.size_%d00", i);
    SET_SPRITE_OPT(city.size_hundreds[i], buffer);
    fc_snprintf(buffer2, sizeof(buffer2), "path.turns_%d00", i);
    SET_SPRITE_ALT_OPT(path.turns_hundreds[i], buffer2, buffer);

    fc_snprintf(buffer, sizeof(buffer), "city.t_food_%d", i);
    SET_SPRITE(city.tile_foodnum[i], buffer);
    fc_snprintf(buffer, sizeof(buffer), "city.t_shields_%d", i);
    SET_SPRITE(city.tile_shieldnum[i], buffer);
    fc_snprintf(buffer, sizeof(buffer), "city.t_trade_%d", i);
    SET_SPRITE(city.tile_tradenum[i], buffer);
  }

  SET_SPRITE(upkeep.unhappy[0], "upkeep.unhappy");
  SET_SPRITE(upkeep.unhappy[1], "upkeep.unhappy2");
  output_type_iterate(o) {
    fc_snprintf(buffer, sizeof(buffer),
                "upkeep.%s", get_output_identifier(o));
    t->sprites.upkeep.output[o][0] = load_sprite(t, buffer);
    fc_snprintf(buffer, sizeof(buffer),
                "upkeep.%s2", get_output_identifier(o));
    t->sprites.upkeep.output[o][1] = load_sprite(t, buffer);
  } output_type_iterate_end;
  
  SET_SPRITE(user.attention, "user.attention");

  SET_SPRITE(tx.fallout,    "tx.fallout");
  SET_SPRITE(tx.pollution,  "tx.pollution");
  SET_SPRITE(tx.village,    "tx.village");
  SET_SPRITE(tx.fog,        "tx.fog");

  sprite_vector_init(&t->sprites.colors.overlays);
  for (i = 0; ; i++) {
    struct sprite *sprite;

    fc_snprintf(buffer, sizeof(buffer), "colors.overlay_%d", i);
    sprite = load_sprite(t, buffer);
    if (!sprite) {
      break;
    }
    sprite_vector_append(&t->sprites.colors.overlays, sprite);
  }
  if (i == 0) {
    log_fatal("Missing overlay-color sprite colors.overlay_0.");
    exit(EXIT_FAILURE);
  }

  /* Chop up and build the overlay graphics. */
  sprite_vector_reserve(&t->sprites.city.worked_tile_overlay,
			sprite_vector_size(&t->sprites.colors.overlays));
  sprite_vector_reserve(&t->sprites.city.unworked_tile_overlay,
			sprite_vector_size(&t->sprites.colors.overlays));
  for (i = 0; i < sprite_vector_size(&t->sprites.colors.overlays); i++) {
    struct sprite *color, *color_mask;
    struct sprite *worked, *unworked;

    color = *sprite_vector_get(&t->sprites.colors.overlays, i);
    color_mask = crop_sprite(color, 0, 0, W, H, t->sprites.mask.tile, 0, 0);
    worked = crop_sprite(color_mask, 0, 0, W, H,
			 t->sprites.mask.worked_tile, 0, 0);
    unworked = crop_sprite(color_mask, 0, 0, W, H,
			   t->sprites.mask.unworked_tile, 0, 0);
    free_sprite(color_mask);
    t->sprites.city.worked_tile_overlay.p[i] =  worked;
    t->sprites.city.unworked_tile_overlay.p[i] = unworked;
  }


  {
    SET_SPRITE(grid.unavailable, "grid.unavailable");
    SET_SPRITE_OPT(grid.nonnative, "grid.nonnative");

    for (i = 0; i < EDGE_COUNT; i++) {
      int j;

      if (i == EDGE_UD && t->hex_width == 0) {
	continue;
      } else if (i == EDGE_LR && t->hex_height == 0) {
	continue;
      }

      fc_snprintf(buffer, sizeof(buffer), "grid.main.%s", edge_name[i]);
      SET_SPRITE(grid.main[i], buffer);

      fc_snprintf(buffer, sizeof(buffer), "grid.city.%s", edge_name[i]);
      SET_SPRITE(grid.city[i], buffer);

      fc_snprintf(buffer, sizeof(buffer), "grid.worked.%s", edge_name[i]);
      SET_SPRITE(grid.worked[i], buffer);

      fc_snprintf(buffer, sizeof(buffer), "grid.selected.%s", edge_name[i]);
      SET_SPRITE(grid.selected[i], buffer);

      fc_snprintf(buffer, sizeof(buffer), "grid.coastline.%s", edge_name[i]);
      SET_SPRITE(grid.coastline[i], buffer);

      for (j = 0; j < 2; j++) {
        fc_snprintf(buffer, sizeof(buffer), "grid.borders.%c",
                    edge_name[i][j]);
        SET_SPRITE(grid.borders[i][j], buffer);
      }
    }
  }

  load_river_sprites(t, &t->sprites.tx.rivers, "tx.river");

  /* We use direction-specific irrigation and farmland graphics, if they
   * are available.  If not, we just fall back to the basic irrigation
   * graphics. */
  for (i = 0; i < t->num_index_cardinal; i++) {
    fc_snprintf(buffer, sizeof(buffer), "tx.s_irrigation_%s",
                cardinal_index_str(t, i));
    SET_SPRITE_ALT(tx.irrigation[i], buffer, "tx.irrigation");
  }
  for (i = 0; i < t->num_index_cardinal; i++) {
    fc_snprintf(buffer, sizeof(buffer), "tx.s_farmland_%s",
                cardinal_index_str(t, i));
    SET_SPRITE_ALT(tx.farmland[i], buffer, "tx.farmland");
  }

  switch (t->darkness_style) {
  case DARKNESS_NONE:
    /* Nothing. */
    break;
  case DARKNESS_ISORECT:
    {
      /* Isometric: take a single tx.darkness tile and split it into 4. */
      struct sprite *darkness = load_sprite(t, "tx.darkness");
      const int W = t->normal_tile_width, H = t->normal_tile_height;
      int offsets[4][2] = {{W / 2, 0}, {0, H / 2}, {W / 2, H / 2}, {0, 0}};

      if (!darkness) {
        log_fatal("Sprite tx.darkness missing.");
        exit(EXIT_FAILURE);
      }
      for (i = 0; i < 4; i++) {
	t->sprites.tx.darkness[i] = crop_sprite(darkness, offsets[i][0],
						offsets[i][1], W / 2, H / 2,
						NULL, 0, 0);
      }
    }
    break;
  case DARKNESS_CARD_SINGLE:
    for (i = 0; i < t->num_cardinal_tileset_dirs; i++) {
      enum direction8 dir = t->cardinal_tileset_dirs[i];

      fc_snprintf(buffer, sizeof(buffer), "tx.darkness_%s",
		  dir_get_tileset_name(dir));
      SET_SPRITE(tx.darkness[i], buffer);
    }
    break;
  case DARKNESS_CARD_FULL:
    for(i = 1; i < t->num_index_cardinal; i++) {
      fc_snprintf(buffer, sizeof(buffer), "tx.darkness_%s",
		  cardinal_index_str(t, i));
      SET_SPRITE(tx.darkness[i], buffer);
    }
    break;
  case DARKNESS_CORNER:
    t->sprites.tx.fullfog = fc_realloc(t->sprites.tx.fullfog,
				       81 * sizeof(*t->sprites.tx.fullfog));
    for (i = 0; i < 81; i++) {
      /* Unknown, fog, known. */
      char ids[] = {'u', 'f', 'k'};
      char buf[512] = "t.fog";
      int values[4], j, k = i;

      for (j = 0; j < 4; j++) {
	values[j] = k % 3;
	k /= 3;

	cat_snprintf(buf, sizeof(buf), "_%c", ids[values[j]]);
      }
      fc_assert(k == 0);

      t->sprites.tx.fullfog[i] = load_sprite(t, buf);
    }
    break;
  };

  /* no other place to initialize these variables */
  sprite_vector_init(&t->sprites.nation_flag);
  sprite_vector_init(&t->sprites.nation_shield);
}

/**************************************************************************
  Load sprites of one river type.
**************************************************************************/
static void load_river_sprites(struct tileset *t,
                               struct river_sprites *store, const char *tag_pfx)
{
  int i;
  const char dir_char[] = "nsew";
  char buffer[512];

  for (i = 0; i < t->num_index_cardinal; i++) {
    fc_snprintf(buffer, sizeof(buffer), "%s_s_%s",
                tag_pfx, cardinal_index_str(t, i));
    store->spec[i] = load_sprite(t, buffer);
  }

  for (i = 0; i < 4; i++) {
    fc_snprintf(buffer, sizeof(buffer), "%s_outlet_%c",
                tag_pfx, dir_char[i]);
    store->outlet[i] = load_sprite(t, buffer);
  }
}

/**************************************************************************
  Frees any internal buffers which are created by load_sprite. Should
  be called after the last (for a given period of time) load_sprite
  call.  This saves a fair amount of memory, but it will take extra time
  the next time we start loading sprites again.
**************************************************************************/
void finish_loading_sprites(struct tileset *t)
{
  specfile_list_iterate(t->specfiles, sf) {
    if (sf->big_sprite) {
      free_sprite(sf->big_sprite);
      sf->big_sprite = NULL;
    }
  } specfile_list_iterate_end;
}

/**********************************************************************
  Load the tiles; requires tilespec_read_toplevel() called previously.
  Leads to tile_sprites being allocated and filled with pointers
  to sprites.   Also sets up and populates sprite_hash, and calls func
  to initialize 'sprites' structure.
***********************************************************************/
void tileset_load_tiles(struct tileset *t)
{
  tileset_lookup_sprite_tags(t);
  finish_loading_sprites(t);
}

/**********************************************************************
  Lookup sprite to match tag, or else to match alt if don't find,
  or else return NULL, and emit log message.
***********************************************************************/
struct sprite *tiles_lookup_sprite_tag_alt(struct tileset *t,
                                           enum log_level level,
                                           const char *tag, const char *alt,
                                           const char *what,
                                           const char *name)
{
  struct sprite *sp;

  /* (should get sprite_hash before connection) */
  fc_assert_ret_val_msg(NULL != t->sprite_hash, NULL,
                        "attempt to lookup for %s \"%s\" before "
                        "sprite_hash setup", what, name);

  sp = load_sprite(t, tag);
  if (sp) return sp;

  sp = load_sprite(t, alt);
  if (sp) {
    log_verbose("Using alternate graphic \"%s\" "
                "(instead of \"%s\") for %s \"%s\".",
                alt, tag, what, name);
    return sp;
  }

  log_base(level, "Don't have graphics tags \"%s\" or \"%s\" for %s \"%s\".",
           tag, alt, what, name);
  if (LOG_FATAL >= level) {
    exit(EXIT_FAILURE);
  }
  return NULL;
}

/**********************************************************************
  Helper function to load sprite for one unit orientation
***********************************************************************/
static bool tileset_setup_unit_direction(struct tileset *t,
                                         int uidx,
                                         const char *base_str,
                                         enum direction8 dir,
                                         const char *dirsuffix)
{
  char buf[2048];

  fc_snprintf(buf, sizeof(buf), "%s_%s", base_str, dirsuffix);

  /* We don't use _alt graphics here, as that could lead to loading
   * real icon gfx, but alternative orientation gfx. Tileset author
   * probably meant icon gfx to be used as fallback for all orientations */
  t->sprites.units.facing[uidx][dir] = load_sprite(t, buf);

  if (t->sprites.units.facing[uidx][dir] != NULL) {
    return TRUE;
  }

  return FALSE;
}

/**********************************************************************
  Try to setup all unit type sprites from single tag
***********************************************************************/
bool static tileset_setup_unit_type_from_tag(struct tileset *t,
                                             int uidx, const char *tag)
{
  bool facing_sprites = TRUE;

  t->sprites.units.icon[uidx] = load_sprite(t, tag);

#define LOAD_FACING_SPRITE(dir, dname)                           \
  if (!tileset_setup_unit_direction(t, uidx, tag, dir, dname)) { \
    facing_sprites = FALSE;                                      \
  }

  LOAD_FACING_SPRITE(DIR8_NORTHWEST, "nw");
  LOAD_FACING_SPRITE(DIR8_NORTH, "n");
  LOAD_FACING_SPRITE(DIR8_NORTHEAST, "ne");
  LOAD_FACING_SPRITE(DIR8_WEST, "w");
  LOAD_FACING_SPRITE(DIR8_EAST, "e");
  LOAD_FACING_SPRITE(DIR8_SOUTHWEST, "sw");
  LOAD_FACING_SPRITE(DIR8_SOUTH, "s");
  LOAD_FACING_SPRITE(DIR8_SOUTHEAST, "se");

  if (!facing_sprites && t->sprites.units.icon[uidx] == NULL) {
    /* Neither icon gfx or orientation sprites */
    return FALSE;
  }

  return TRUE;

#undef LOAD_FACING_SPRITE
}

/**********************************************************************
  Set unit_type sprite value; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tileset_setup_unit_type(struct tileset *t, struct unit_type *ut)
{
  int uidx = utype_index(ut);

  if (!tileset_setup_unit_type_from_tag(t, uidx, ut->graphic_str)
      && !tileset_setup_unit_type_from_tag(t, uidx, ut->graphic_alt)) {
    log_fatal("Missing %s unit tag \"%s\" and alternative \"%s\".",
              utype_rule_name(ut), ut->graphic_str, ut->graphic_alt);
    exit(EXIT_FAILURE);
  }
}

/**********************************************************************
  Set improvement_type sprite value; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tileset_setup_impr_type(struct tileset *t,
			     struct impr_type *pimprove)
{
  t->sprites.building[improvement_index(pimprove)] =
    tiles_lookup_sprite_tag_alt(t, LOG_VERBOSE, pimprove->graphic_str,
				pimprove->graphic_alt, "improvement",
				improvement_rule_name(pimprove));

  /* should maybe do something if NULL, eg generic default? */
}

/**********************************************************************
  Set tech_type sprite value; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tileset_setup_tech_type(struct tileset *t,
			     struct advance *padvance)
{
  if (valid_advance(padvance)) {
    t->sprites.tech[advance_index(padvance)] =
      tiles_lookup_sprite_tag_alt(t, LOG_VERBOSE, padvance->graphic_str,
				  padvance->graphic_alt, "technology",
				  advance_rule_name(padvance));

    /* should maybe do something if NULL, eg generic default? */
  } else {
    t->sprites.tech[advance_index(padvance)] = NULL;
  }
}

/****************************************************************************
  Set resource sprite values; should only happen after
  tilespec_load_tiles().
****************************************************************************/
void tileset_setup_resource(struct tileset *t,
                            const struct resource *presource)
{
  fc_assert_ret(NULL != presource);
  t->sprites.resource[resource_index(presource)] =
    tiles_lookup_sprite_tag_alt(t, LOG_VERBOSE, presource->graphic_str,
                                presource->graphic_alt, "resource",
                                resource_rule_name(presource));
}

/****************************************************************************
  Set road sprite values; should only happen after
  tilespec_load_tiles().
****************************************************************************/
void tileset_setup_road(struct tileset *t,
                        struct road_type *proad)
{
  char full_tag_name[MAX_LEN_NAME + strlen("r._isolated")];
  char full_alt_name[MAX_LEN_NAME + strlen("r._isolated")];
  const int id = road_index(proad);
  int i;
  enum roadstyle_id *roadstyle;

  if (!rstyle_hash_lookup(t->rstyle_hash, proad->graphic_str,
                          &roadstyle)
      && !rstyle_hash_lookup(t->rstyle_hash, proad->graphic_alt,
                             &roadstyle)) {
    log_fatal("No roadstyle for \"%s\" or \"%s\".",
              proad->graphic_str,
              proad->graphic_alt);
    exit(EXIT_FAILURE);
  }

  t->sprites.roads[id].roadstyle = *roadstyle;

  if (*roadstyle == RSTYLE_RIVER) {
    road_type_list_append(t->rivers, proad);
  }

  /* Isolated road graphics are used by RSTYLE_ALL_SEPARATE and
     RSTYLE_PARITY_COMBINED. */
  if (*roadstyle == RSTYLE_ALL_SEPARATE || *roadstyle == RSTYLE_PARITY_COMBINED) {
    fc_snprintf(full_tag_name, sizeof(full_tag_name),
                "r.%s_isolated", proad->graphic_str);
    fc_snprintf(full_alt_name, sizeof(full_alt_name),
                "r.%s_isolated", proad->graphic_alt);

    SET_SPRITE_ALT(roads[id].isolated, full_tag_name, full_alt_name);
  }

  if (*roadstyle == RSTYLE_ALL_SEPARATE) {
    /* RSTYLE_ALL_SEPARATE has just 8 additional sprites for each road type:
     * one going off in each direction. */
    for (i = 0; i < t->num_valid_tileset_dirs; i++) {
      enum direction8 dir = t->valid_tileset_dirs[i];
      const char *dir_name = dir_get_tileset_name(dir);

      fc_snprintf(full_tag_name, sizeof(full_tag_name),
                  "r.%s_%s", proad->graphic_str, dir_name);
      fc_snprintf(full_alt_name, sizeof(full_alt_name),
                  "r.%s_%s", proad->graphic_alt, dir_name);

      SET_SPRITE_ALT(roads[id].u.dir[i], full_tag_name, full_alt_name);
    }
  } else if (*roadstyle == RSTYLE_PARITY_COMBINED) {
    int num_index = 1 << (t->num_valid_tileset_dirs / 2), j;

    /* RSTYLE_PARITY_COMBINED has 32 additional sprites for each road type:
     * 16 each for cardinal and diagonal directions.  Each set
     * of 16 provides a NSEW-indexed sprite to provide connectors for
     * all rails in the cardinal/diagonal directions.  The 0 entry is
     * unused (the "isolated" sprite is used instead). */

    for (i = 1; i < num_index; i++) {
      char c[64] = "", d[64] = "";

      for (j = 0; j < t->num_valid_tileset_dirs / 2; j++) {
	int value = (i >> j) & 1;

	cat_snprintf(c, sizeof(c), "%s%d",
		     dir_get_tileset_name(t->valid_tileset_dirs[2 * j]),
		     value);
	cat_snprintf(d, sizeof(d), "%s%d",
		     dir_get_tileset_name(t->valid_tileset_dirs[2 * j + 1]),
		     value);
      }

      fc_snprintf(full_tag_name, sizeof(full_tag_name),
                  "r.c_%s_%s", proad->graphic_str, c);
      fc_snprintf(full_alt_name, sizeof(full_alt_name),
                  "r.c_%s_%s", proad->graphic_alt, c);

      SET_SPRITE_ALT(roads[id].u.combo.even[i], full_tag_name, full_alt_name);

      fc_snprintf(full_tag_name, sizeof(full_tag_name),
                  "r.d_%s_%s", proad->graphic_str, d);
      fc_snprintf(full_alt_name, sizeof(full_alt_name),
                  "r.d_%s_%s", proad->graphic_alt, d);

      SET_SPRITE_ALT(roads[id].u.combo.odd[i], full_tag_name, full_alt_name);
    }
  } else if (*roadstyle == RSTYLE_ALL_COMBINED) {
    /* RSTYLE_ALL_COMBINED includes 256 sprites, one for every possibility.
     * Just go around clockwise, with all combinations. */
    for (i = 0; i < t->num_index_valid; i++) {
      char *idx_str = valid_index_str(t, i);

      fc_snprintf(full_tag_name, sizeof(full_tag_name),
                  "r.%s_%s", proad->graphic_str, idx_str);
      fc_snprintf(full_alt_name, sizeof(full_alt_name),
                  "r.%s_%s", proad->graphic_alt, idx_str);

      SET_SPRITE_ALT(roads[id].u.total[i], full_tag_name, full_alt_name);
    }
  } else if (*roadstyle == RSTYLE_RIVER) {
    load_river_sprites(t, &t->sprites.roads[id].u.rivers, "tx.river");
  } else {
    fc_assert(FALSE);
  }

  /* Corner road graphics are used by RSTYLE_ALL_SEPARATE and
   * RSTYLE_PARITY_COMBINED. */
  if (*roadstyle == RSTYLE_ALL_SEPARATE || *roadstyle == RSTYLE_PARITY_COMBINED) {
    for (i = 0; i < t->num_valid_tileset_dirs; i++) {
      enum direction8 dir = t->valid_tileset_dirs[i];

      if (!is_cardinal_tileset_dir(t, dir)) {
        const char *dtn = dir_get_tileset_name(dir);

        fc_snprintf(full_tag_name, sizeof(full_tag_name),
                    "r.c_%s_%s", proad->graphic_str, dtn);
        fc_snprintf(full_alt_name, sizeof(full_alt_name),
                    "r.c_%s_%s", proad->graphic_alt, dtn);

        SET_SPRITE_ALT_OPT(roads[id].corner[dir], full_tag_name, full_alt_name);
      }
    }
  }

  t->sprites.roads[id].activity = load_sprite(t, proad->activity_gfx);
  if (t->sprites.roads[id].activity == NULL) {
    t->sprites.roads[id].activity = load_sprite(t, proad->act_gfx_alt);
    if (t->sprites.roads[id].activity == NULL) {
      log_fatal("Missing %s building activity tag \"%s\" and alternative \"%s\".",
                road_rule_name(proad), proad->activity_gfx, proad->act_gfx_alt);
      exit(EXIT_FAILURE);
    }
  }
}

/****************************************************************************
  Set base sprite values; should only happen after
  tilespec_load_tiles().
****************************************************************************/
void tileset_setup_base(struct tileset *t,
                        const struct base_type *pbase)
{
  char full_tag_name[MAX_LEN_NAME + strlen("_fg")];
  const int id = base_index(pbase);

  fc_assert_ret(id >= 0 && id < base_count());

  sz_strlcpy(full_tag_name, pbase->graphic_str);
  strcat(full_tag_name, "_bg");
  t->sprites.bases[id].background = load_sprite(t, full_tag_name);

  sz_strlcpy(full_tag_name, pbase->graphic_str);
  strcat(full_tag_name, "_mg");
  t->sprites.bases[id].middleground = load_sprite(t, full_tag_name);

  sz_strlcpy(full_tag_name, pbase->graphic_str);
  strcat(full_tag_name, "_fg");
  t->sprites.bases[id].foreground = load_sprite(t, full_tag_name);

  if (t->sprites.bases[id].background == NULL
      && t->sprites.bases[id].middleground == NULL
      && t->sprites.bases[id].foreground == NULL) {
    /* No primary graphics at all. Try alternative */
    log_verbose("Using alternate graphic \"%s\" "
                "(instead of \"%s\") for base \"%s\".",
                pbase->graphic_alt, pbase->graphic_str,
                base_rule_name(pbase));

    sz_strlcpy(full_tag_name, pbase->graphic_alt);
    strcat(full_tag_name, "_bg");
    t->sprites.bases[id].background = load_sprite(t, full_tag_name);

    sz_strlcpy(full_tag_name, pbase->graphic_alt);
    strcat(full_tag_name, "_mg");
    t->sprites.bases[id].middleground = load_sprite(t, full_tag_name);

    sz_strlcpy(full_tag_name, pbase->graphic_alt);
    strcat(full_tag_name, "_fg");
    t->sprites.bases[id].foreground = load_sprite(t, full_tag_name);

    if (t->sprites.bases[id].background == NULL
        && t->sprites.bases[id].middleground == NULL
        && t->sprites.bases[id].foreground == NULL) {
      /* Cannot find alternative graphics either */
      log_fatal("No graphics for base \"%s\" at all!",
                base_rule_name(pbase));
      exit(EXIT_FAILURE);
    }
  }

  t->sprites.bases[id].activity = load_sprite(t, pbase->activity_gfx);
  if (t->sprites.bases[id].activity == NULL) {
    t->sprites.bases[id].activity = load_sprite(t, pbase->act_gfx_alt);
    if (t->sprites.bases[id].activity == NULL) {
      log_fatal("Missing %s building activity tag \"%s\" and alternative \"%s\".",
                base_rule_name(pbase), pbase->activity_gfx, pbase->act_gfx_alt);
      exit(EXIT_FAILURE);
    }
  }
}


/**********************************************************************
  Set tile_type sprite values; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tileset_setup_tile_type(struct tileset *t,
			     const struct terrain *pterrain)
{
  struct drawing_data *draw;
  struct sprite *sprite;
  char buffer[MAX_LEN_NAME + 20];
  int i, l;
  
  if (0 == strlen(terrain_rule_name(pterrain))) {
    return;
  }

  if (!drawing_hash_lookup(t->tile_hash, pterrain->graphic_str, &draw)
      && !drawing_hash_lookup(t->tile_hash, pterrain->graphic_alt, &draw)) {
    log_fatal("Terrain \"%s\": no graphic tile \"%s\" or \"%s\".",
              terrain_rule_name(pterrain), pterrain->graphic_str,
              pterrain->graphic_alt);
    exit(EXIT_FAILURE);
  }

  /* Set up each layer of the drawing. */
  for (l = 0; l < draw->num_layers; l++) {
    struct drawing_layer *dlp = &draw->layer[l];
    struct tileset_layer *tslp = &t->layers[l];
    sprite_vector_init(&dlp->base);
    sprite_vector_init(&dlp->allocated);

    switch (dlp->sprite_type) {
    case CELL_WHOLE:
      switch (dlp->match_style) {
      case MATCH_NONE:
	/* Load whole sprites for this tile. */
	for (i = 0; ; i++) {
          fc_snprintf(buffer, sizeof(buffer), "t.l%d.%s%d",
                      l, draw->name, i + 1);
	  sprite = load_sprite(t, buffer);
	  if (!sprite) {
	    break;
	  }
	  sprite_vector_reserve(&dlp->base, i + 1);
	  dlp->base.p[i] = sprite;
	}
	/* check for base sprite, allowing missing sprites above base */
	if (0 == i  &&  0 == l) {
          log_fatal("Missing base sprite tag \"%s\".", buffer);
	  exit(EXIT_FAILURE);
	}
	break;
      case MATCH_SAME:
	/* Load 16 cardinally-matched sprites. */
	for (i = 0; i < t->num_index_cardinal; i++) {
          fc_snprintf(buffer, sizeof(buffer), "t.l%d.%s_%s",
                      l, draw->name, cardinal_index_str(t, i));
	  dlp->match[i] =
	    tiles_lookup_sprite_tag_alt(t, LOG_FATAL, buffer, "",
					"matched terrain",
					terrain_rule_name(pterrain));
	}
	break;
      case MATCH_PAIR:
      case MATCH_FULL:
        fc_assert(FALSE); /* not yet defined */
        break;
      };
      break;
    case CELL_CORNER:
      {
	const int count = dlp->match_indices;
	int number = NUM_CORNER_DIRS;

	switch (dlp->match_style) {
	case MATCH_NONE:
	  /* do nothing */
	  break;
	case MATCH_PAIR:
	case MATCH_SAME:
	  /* N directions (NSEW) * 3 dimensions of matching */
          fc_assert(count == 2);
	  number = NUM_CORNER_DIRS * 2 * 2 * 2;
	  break;
	case MATCH_FULL:
	default:
	  /* N directions (NSEW) * 3 dimensions of matching */
	  /* could use exp() or expi() here? */
	  number = NUM_CORNER_DIRS * count * count * count;
	  break;
	};

	dlp->cells
	  = fc_calloc(number, sizeof(*dlp->cells));

	for (i = 0; i < number; i++) {
	  enum direction4 dir = i % NUM_CORNER_DIRS;
	  int value = i / NUM_CORNER_DIRS;

	  switch (dlp->match_style) {
	  case MATCH_NONE:
            fc_snprintf(buffer, sizeof(buffer), "t.l%d.%s_cell_%c",
                        l, draw->name, direction4letters[dir]);
	    dlp->cells[i] =
	      tiles_lookup_sprite_tag_alt(t, LOG_FATAL, buffer, "",
					  "cell terrain",
					  terrain_rule_name(pterrain));
	    break;
	  case MATCH_SAME:
            fc_snprintf(buffer, sizeof(buffer), "t.l%d.%s_cell_%c%d%d%d",
                        l, draw->name, direction4letters[dir],
                        (value) & 1, (value >> 1) & 1, (value >> 2) & 1);
	    dlp->cells[i] =
	      tiles_lookup_sprite_tag_alt(t, LOG_FATAL, buffer, "",
					  "same cell terrain",
					  terrain_rule_name(pterrain));
	    break;
	  case MATCH_PAIR:
            fc_snprintf(buffer, sizeof(buffer), "t.l%d.%s_cell_%c_%c_%c_%c",
                        l, draw->name, direction4letters[dir],
                        tslp->match_types[dlp->match_index[(value) & 1]][0],
                        tslp->match_types[dlp->match_index[(value >> 1) & 1]][0],
                        tslp->match_types[dlp->match_index[(value >> 2) & 1]][0]);
	    dlp->cells[i] =
	      tiles_lookup_sprite_tag_alt(t, LOG_FATAL, buffer, "",
					  "cell pair terrain",
					  terrain_rule_name(pterrain));
	    break;
	  case MATCH_FULL:
	    {
	      int this = dlp->match_index[0];
	      int n, s, e, w;
	      int v1, v2, v3;

	      v1 = dlp->match_index[value % count];
	      value /= count;
	      v2 = dlp->match_index[value % count];
	      value /= count;
	      v3 = dlp->match_index[value % count];

              fc_assert(v1 < count && v2 < count && v3 < count);

	      /* Assume merged cells.  This should be a separate option. */
	      switch (dir) {
	      case DIR4_NORTH:
		s = this;
		w = v1;
		n = v2;
		e = v3;
		break;
	      case DIR4_EAST:
		w = this;
		n = v1;
		e = v2;
		s = v3;
		break;
	      case DIR4_SOUTH:
		n = this;
		e = v1;
		s = v2;
		w = v3;
		break;
	      case DIR4_WEST:
	      default:		/* avoid warnings */
		e = this;
		s = v1;
		w = v2;
		n = v3;
		break;
	      };

	      /* Use first character of match_types,
	       * already checked for uniqueness. */
              fc_snprintf(buffer, sizeof(buffer),
                          "t.l%d.cellgroup_%c_%c_%c_%c", l,
                          tslp->match_types[n][0], tslp->match_types[e][0],
                          tslp->match_types[s][0], tslp->match_types[w][0]);
	      sprite = load_sprite(t, buffer);

	      if (sprite) {
		/* Crop the sprite to separate this cell. */
                int vec_size = sprite_vector_size(&dlp->allocated);

		const int W = t->normal_tile_width;
		const int H = t->normal_tile_height;
		int x[4] = {W / 4, W / 4, 0, W / 2};
		int y[4] = {H / 2, 0, H / 4, H / 4};
		int xo[4] = {0, 0, -W / 2, W / 2};
		int yo[4] = {H / 2, -H / 2, 0, 0};

		sprite = crop_sprite(sprite,
				     x[dir], y[dir], W / 2, H / 2,
				     t->sprites.mask.tile,
				     xo[dir], yo[dir]);

                /* We allocated new sprite with crop_sprite. Store its
                 * address so we can free it. */
                sprite_vector_reserve(&dlp->allocated, vec_size + 1);
                dlp->allocated.p[vec_size] = sprite;
	      } else {
                log_error("Terrain graphics tag \"%s\" missing.", buffer);
	      }

	      dlp->cells[i] = sprite;
	    }
	    break;
	  };
	}
      }
      break;
    };
  }

  /* try an optional special name */
  fc_snprintf(buffer, sizeof(buffer), "t.blend.%s", draw->name);
  draw->blender =
    tiles_lookup_sprite_tag_alt(t, LOG_VERBOSE, buffer, "",
				"blend terrain",
				terrain_rule_name(pterrain));

  if (draw->blending > 0) {
    const int l = draw->blending - 1;

    if (NULL == draw->blender) {
      int i = 0;
      /* try an already loaded base */
      while (NULL == draw->blender
        &&  i < draw->blending
        &&  0 < draw->layer[i].base.size) {
        draw->blender = draw->layer[i++].base.p[0];
      }
    }

    if (NULL == draw->blender) {
      /* try an unloaded base name */
      fc_snprintf(buffer, sizeof(buffer), "t.l%d.%s1", l, draw->name);
      draw->blender =
	tiles_lookup_sprite_tag_alt(t, LOG_FATAL, buffer, "",
				    "base (blend) terrain",
				    terrain_rule_name(pterrain));
    }
  }

  if (NULL != draw->blender) {
    /* Set up blending sprites. This only works in iso-view! */
    const int W = t->normal_tile_width;
    const int H = t->normal_tile_height;
    const int offsets[4][2] = {
      {W / 2, 0}, {0, H / 2}, {W / 2, H / 2}, {0, 0}
    };
    enum direction4 dir = 0;

    for (; dir < 4; dir++) {
      draw->blend[dir] = crop_sprite(draw->blender,
				     offsets[dir][0], offsets[dir][1],
				     W / 2, H / 2,
				     t->sprites.dither_tile, 0, 0);
    }
  }

  if (draw->mine_tag) {
    draw->mine = load_sprite(t, draw->mine_tag);
  } else {
    draw->mine = NULL;
  }

  t->sprites.drawing[terrain_index(pterrain)] = draw;
}

/**********************************************************************
  Set government sprite value; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tileset_setup_government(struct tileset *t,
			      struct government *gov)
{
  t->sprites.government[government_index(gov)] =
    tiles_lookup_sprite_tag_alt(t, LOG_FATAL, gov->graphic_str,
				gov->graphic_alt, "government",
				government_rule_name(gov));
  
  /* should probably do something if NULL, eg generic default? */
}

/**********************************************************************
  Set nation flag sprite value; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tileset_setup_nation_flag(struct tileset *t, 
			       struct nation_type *nation)
{
  char *tags[] = {nation->flag_graphic_str,
		  nation->flag_graphic_alt,
		  "unknown", NULL};
  int i;
  struct sprite *flag = NULL, *shield = NULL;
  char buf[1024];

  for (i = 0; tags[i] && !flag; i++) {
    fc_snprintf(buf, sizeof(buf), "f.%s", tags[i]);
    flag = load_sprite(t, buf);
  }
  for (i = 0; tags[i] && !shield; i++) {
    fc_snprintf(buf, sizeof(buf), "f.shield.%s", tags[i]);
    shield = load_sprite(t, buf);
  }
  if (!flag || !shield) {
    /* Should never get here because of the f.unknown fallback. */
    log_fatal("Nation %s: no national flag.", nation_rule_name(nation));
    exit(EXIT_FAILURE);
  }

  sprite_vector_reserve(&t->sprites.nation_flag, nation_count());
  t->sprites.nation_flag.p[nation_index(nation)] = flag;

  sprite_vector_reserve(&t->sprites.nation_shield, nation_count());
  t->sprites.nation_shield.p[nation_index(nation)] = shield;
}

/**********************************************************************
  Return the flag graphic to be used by the city.
***********************************************************************/
struct sprite *get_city_flag_sprite(const struct tileset *t,
				    const struct city *pcity)
{
  return get_nation_flag_sprite(t, nation_of_city(pcity));
}

/**********************************************************************
  Return a sprite for the national flag for this unit.
***********************************************************************/
static struct sprite *get_unit_nation_flag_sprite(const struct tileset *t,
						  const struct unit *punit)
{
  struct nation_type *pnation = nation_of_unit(punit);

  if (draw_unit_shields) {
    return t->sprites.nation_shield.p[nation_index(pnation)];
  } else {
    return t->sprites.nation_flag.p[nation_index(pnation)];
  }
}

#define FULL_TILE_X_OFFSET ((t->normal_tile_width - t->full_tile_width) / 2)
#define FULL_TILE_Y_OFFSET (t->normal_tile_height - t->full_tile_height)

#define ADD_SPRITE(s, draw_fog, x_offset, y_offset)			    \
  (fc_assert(s != NULL),						    \
   sprs->sprite = s,							    \
   sprs->foggable = (draw_fog && t->fogstyle == FOG_AUTO),		    \
   sprs->offset_x = x_offset,						    \
   sprs->offset_y = y_offset,						    \
   sprs++)
#define ADD_SPRITE_SIMPLE(s) ADD_SPRITE(s, TRUE, 0, 0)
#define ADD_SPRITE_FULL(s)						    \
  ADD_SPRITE(s, TRUE, FULL_TILE_X_OFFSET, FULL_TILE_Y_OFFSET)

/**************************************************************************
  Assemble some data that is used in building the tile sprite arrays.
    (map_x, map_y) : the (normalized) map position
  The values we fill in:
    tterrain_near  : terrain types of all adjacent terrain
    tspecial_near  : specials of all adjacent terrain
**************************************************************************/
static void build_tile_data(const struct tile *ptile,
			    struct terrain *pterrain,
			    struct terrain **tterrain_near,
			    bv_special *tspecial_near,
                            bv_roads *troad_near)
{
  enum direction8 dir;

  /* Loop over all adjacent tiles.  We should have an iterator for this. */
  for (dir = 0; dir < 8; dir++) {
    struct tile *tile1 = mapstep(ptile, dir);

    if (tile1 && client_tile_get_known(tile1) != TILE_UNKNOWN) {
      struct terrain *terrain1 = tile_terrain(tile1);

      if (NULL != terrain1) {
        tterrain_near[dir] = terrain1;
        tspecial_near[dir] = tile_specials(tile1);
        troad_near[dir] = tile_roads(tile1);
        continue;
      }
      log_error("build_tile_data() tile (%d,%d) has no terrain!",
                TILE_XY(tile1));
    }
    /* At the edges of the (known) map, pretend the same terrain continued
     * past the edge of the map. */
    tterrain_near[dir] = pterrain;
    BV_CLR_ALL(tspecial_near[dir]);
    BV_CLR_ALL(troad_near[dir]);
  }
}

/**********************************************************************
  Fill in the sprite array for the unit type.
***********************************************************************/
static int fill_unit_type_sprite_array(const struct tileset *t,
                                       struct drawn_sprite *sprs,
                                       const struct unit_type *putype,
                                       enum direction8 facing)
{
  struct drawn_sprite *save_sprs = sprs;
  struct sprite *uspr = get_unittype_sprite(t, putype, facing);

  ADD_SPRITE(uspr, TRUE,
             FULL_TILE_X_OFFSET + t->unit_offset_x,
             FULL_TILE_Y_OFFSET + t->unit_offset_y);

  return sprs - save_sprs;
}

/**********************************************************************
  Fill in the sprite array for the unit.
***********************************************************************/
static int fill_unit_sprite_array(const struct tileset *t,
                                  struct drawn_sprite *sprs,
                                  const struct unit *punit,
                                  bool stack, bool backdrop)
{
  struct drawn_sprite *save_sprs = sprs;
  int ihp;
  struct unit_type *ptype = unit_type(punit);

  if (backdrop) {
    if (!solid_color_behind_units) {
      ADD_SPRITE(get_unit_nation_flag_sprite(t, punit), TRUE,
		 FULL_TILE_X_OFFSET + t->unit_flag_offset_x,
		 FULL_TILE_Y_OFFSET + t->unit_flag_offset_y);
    } else {
      /* Taken care of in the LAYER_BACKGROUND. */
    }
  }

  /* Add the sprite for the unit type. */
  sprs += fill_unit_type_sprite_array(t, sprs, ptype,
                                      punit->facing);

  if (t->sprites.unit.loaded && unit_transported(punit)) {
    ADD_SPRITE_FULL(t->sprites.unit.loaded);
  }

  if(punit->activity!=ACTIVITY_IDLE) {
    struct sprite *s = NULL;
    switch(punit->activity) {
    case ACTIVITY_MINE:
      s = t->sprites.unit.mine;
      break;
    case ACTIVITY_POLLUTION:
      s = t->sprites.unit.pollution;
      break;
    case ACTIVITY_FALLOUT:
      s = t->sprites.unit.fallout;
      break;
    case ACTIVITY_PILLAGE:
      s = t->sprites.unit.pillage;
      break;
    case ACTIVITY_IRRIGATE:
      s = t->sprites.unit.irrigate;
      break;
    case ACTIVITY_EXPLORE:
      s = t->sprites.unit.auto_explore;
      break;
    case ACTIVITY_FORTIFIED:
      s = t->sprites.unit.fortified;
      break;
    case ACTIVITY_FORTIFYING:
      s = t->sprites.unit.fortifying;
      break;
    case ACTIVITY_SENTRY:
      s = t->sprites.unit.sentry;
      break;
    case ACTIVITY_GOTO:
      s = t->sprites.unit.go_to;
      break;
    case ACTIVITY_TRANSFORM:
      s = t->sprites.unit.transform;
      break;
    case ACTIVITY_BASE:
      s = t->sprites.bases[punit->activity_target.obj.base].activity;
      break;
    case ACTIVITY_GEN_ROAD:
      s = t->sprites.roads[punit->activity_target.obj.road].activity;
      break;
    case ACTIVITY_CONVERT:
      s = t->sprites.unit.convert;
      break;
    default:
      break;
    }

    ADD_SPRITE_FULL(s);
  }

  if (punit->ai_controlled && punit->activity != ACTIVITY_EXPLORE) {
    if (is_military_unit(punit)) {
      ADD_SPRITE_FULL(t->sprites.unit.auto_attack);
    } else {
      ADD_SPRITE_FULL(t->sprites.unit.auto_settler);
    }
  }

  if (unit_has_orders(punit)) {
    if (punit->orders.repeat) {
      ADD_SPRITE_FULL(t->sprites.unit.patrol);
    } else if (punit->activity != ACTIVITY_IDLE) {
      ADD_SPRITE_SIMPLE(t->sprites.unit.connect);
    } else {
      ADD_SPRITE_FULL(t->sprites.unit.go_to);
    }
  }

  if (punit->battlegroup != BATTLEGROUP_NONE) {
    ADD_SPRITE_FULL(t->sprites.unit.battlegroup[punit->battlegroup]);
  }

  if (t->sprites.unit.lowfuel
      && utype_fuel(ptype)
      && punit->fuel == 1
      && punit->moves_left <= 2 * SINGLE_MOVE) {
    /* Show a low-fuel graphic if the plane has 2 or fewer moves left. */
    ADD_SPRITE_FULL(t->sprites.unit.lowfuel);
  }
  if (t->sprites.unit.tired
      && punit->moves_left < SINGLE_MOVE
      && ptype->move_rate > 0) {
    /* Show a "tired" graphic if the unit has fewer than one move
     * remaining, except for units for which it's full movement. */
    ADD_SPRITE_FULL(t->sprites.unit.tired);
  }

  if (stack || punit->client.occupied) {
    ADD_SPRITE_FULL(t->sprites.unit.stack);
  }

  if (t->sprites.unit.vet_lev[punit->veteran]) {
    ADD_SPRITE_FULL(t->sprites.unit.vet_lev[punit->veteran]);
  }

  ihp = ((NUM_TILES_HP_BAR-1)*punit->hp) / ptype->hp;
  ihp = CLIP(0, ihp, NUM_TILES_HP_BAR-1);
  ADD_SPRITE_FULL(t->sprites.unit.hp_bar[ihp]);

  return sprs - save_sprs;
}

/**************************************************************************
  Add any corner road sprites to the sprite array.
**************************************************************************/
static int fill_road_corner_sprites(const struct tileset *t,
                                    const struct road_type *proad,
				    struct drawn_sprite *sprs,
				    bool road, bool *road_near,
				    bool hider, bool *hider_near)
{
  struct drawn_sprite *saved_sprs = sprs;
  int i;
  int road_idx = road_index(proad);

  if (road_has_flag(proad, RF_CARDINAL_ONLY)) {
    return 0;
  }

  fc_assert_ret_val(draw_roads_rails, 0);

  /* Roads going diagonally adjacent to this tile need to be
   * partly drawn on this tile. */

  /* Draw the corner sprite if:
   *   - There is a diagonal road (not rail!) between two adjacent tiles.
   *   - There is no diagonal road (not rail!) that intersects this road.
   * The logic is simple: roads are drawn underneath railrods, but are
   * not always covered by them (even in the corners!).  But if a railroad
   * connects two tiles, only the railroad (no road) is drawn between
   * those tiles.
   */
  for (i = 0; i < t->num_valid_tileset_dirs; i++) {
    enum direction8 dir = t->valid_tileset_dirs[i];

    if (!is_cardinal_tileset_dir(t, dir)) {
      /* Draw corner sprites for this non-cardinal direction. */
      int cw = (i + 1) % t->num_valid_tileset_dirs;
      int ccw
	= (i + t->num_valid_tileset_dirs - 1) % t->num_valid_tileset_dirs;
      enum direction8 dir_cw = t->valid_tileset_dirs[cw];
      enum direction8 dir_ccw = t->valid_tileset_dirs[ccw];

      if (t->sprites.roads[road_idx].corner[dir]
	  && (road_near[dir_cw] && road_near[dir_ccw]
	      && !(hider_near[dir_cw] && hider_near[dir_ccw]))
	  && !(road && road_near[dir] && !(hider && hider_near[dir]))) {
	ADD_SPRITE_SIMPLE(t->sprites.roads[road_idx].corner[dir]);
      }
    }
  }

  return sprs - saved_sprs;
}

/**************************************************************************
  Fill all road and rail sprites into the sprite array.
**************************************************************************/
static int fill_road_sprite_array(const struct tileset *t,
                                  const struct road_type *proad,
                                  struct drawn_sprite *sprs,
                                  bv_roads troad,
                                  bv_roads *troad_near,
                                  struct terrain *tterrain_near[8],
                                  const struct city *pcity)
{
  struct drawn_sprite *saved_sprs = sprs;
  bool road, road_near[8], hider, hider_near[8];
  bool land_near[8], hland_near[8];
  bool draw_road[8], draw_single_road;
  enum direction8 dir;
  int road_idx = -1;
  bool cl = FALSE;
  enum roadstyle_id roadstyle;

  if (!draw_roads_rails) {
    /* Don't draw anything. */
    return 0;
  }

  road_idx = road_index(proad);

  roadstyle = t->sprites.roads[road_idx].roadstyle;

  if (roadstyle == RSTYLE_RIVER) {
    return 0;
  }

  if (road_has_flag(proad, RF_CONNECT_LAND)) {
    cl = TRUE;
  } else {
    int i;

    for (i = 0; i < 8; i++) {
      land_near[i] = FALSE;
    }
  }

  /* Fill some data arrays. rail_near and road_near store whether road/rail
   * is present in the given direction.  draw_rail and draw_road store
   * whether road/rail is to be drawn in that direction.  draw_single_road
   * and draw_single_rail store whether we need an isolated road/rail to be
   * drawn. */
  road = BV_ISSET(troad, road_idx);

  hider = FALSE;
  road_type_list_iterate(proad->hiders, phider) {
    if (BV_ISSET(troad, road_index(phider))) {
      hider = TRUE;
      break;
    }
  } road_type_list_iterate_end;

  if (road && (!pcity || !draw_cities) && !hider) {
    draw_single_road = TRUE;
  } else {
    draw_single_road = FALSE;
  }

  for (dir = 0; dir < 8; dir++) {
    bool roads_exist;

    /* Check if there is adjacent road/rail. */
    if (!road_has_flag(proad, RF_CARDINAL_ONLY)
        || is_cardinal_tileset_dir(t, dir)) {
      road_near[dir] = BV_ISSET(troad_near[dir], road_idx);
      if (cl) {
        land_near[dir] = (tterrain_near[dir] != T_UNKNOWN
                          && terrain_type_terrain_class(tterrain_near[dir]) != TC_OCEAN);
      }
    } else {
      road_near[dir] = FALSE;
      land_near[dir] = FALSE;
    }

    /* Draw rail/road if there is a connection from this tile to the
     * adjacent tile.  But don't draw road if there is also a rail
     * connection. */
    roads_exist = road && (road_near[dir] || land_near[dir]);
    draw_road[dir] = roads_exist;
    hider_near[dir] = FALSE;
    hland_near[dir] = tterrain_near[dir] != T_UNKNOWN
      && terrain_type_terrain_class(tterrain_near[dir]) != TC_OCEAN;
    road_type_list_iterate(proad->hiders, phider) {
      bool hider_dir = FALSE;
      bool land_dir = FALSE;

      if (!road_has_flag(phider, RF_CARDINAL_ONLY)
          || is_cardinal_tileset_dir(t, dir)) {
        if (BV_ISSET(troad_near[dir], road_index(phider))) {
          hider_near[dir] = TRUE;
          hider_dir = TRUE;
        }
        if (hland_near[dir]
            && road_has_flag(phider, RF_CONNECT_LAND)) {
          land_dir = TRUE;
        }
        if (hider_dir || land_dir) {
          if (BV_ISSET(troad, road_index(phider))) {
            draw_road[dir] = FALSE;
          }
        }
      }
    } road_type_list_iterate_end;

    /* Don't draw an isolated road/rail if there's any connection.
     * draw_single_road would be true in the first place only if start tile has road,
     * so it will have road connection with any adjacent road tile. We check from real
     * existence of road (road_near[dir]) and not from whether road gets drawn (draw_road[dir])
     * as latter can be FALSE when road is simply hidden by another one, and we don't want to
     * draw single road in that case either. */
    if (draw_single_road && road_near[dir]) {
      draw_single_road = FALSE;
    }
  }

  /* Draw road corners */
  sprs
    += fill_road_corner_sprites(t, proad, sprs, road, road_near, hider, hider_near);

  if (roadstyle == RSTYLE_ALL_SEPARATE) {
    /* With RSTYLE_ALL_SEPARATE, we simply draw one road for every connection.
     * This means we only need a few sprites, but a lot of drawing is
     * necessary and it generally doesn't look very good. */
    int i;

    /* First draw roads under rails. */
    if (road) {
      for (i = 0; i < t->num_valid_tileset_dirs; i++) {
	if (draw_road[t->valid_tileset_dirs[i]]) {
	  ADD_SPRITE_SIMPLE(t->sprites.roads[road_idx].u.dir[i]);
	}
      }
    }
  } else if (roadstyle == RSTYLE_PARITY_COMBINED) {
    /* With RSTYLE_PARITY_COMBINED, we draw one sprite for cardinal road connections,
     * one sprite for diagonal road connections, and the same for rail.
     * This means we need about 4x more sprites than in style 0, but up to
     * 4x less drawing is needed.  The drawing quality may also be
     * improved. */

    /* First draw roads under rails. */
    if (road) {
      int road_even_tileno = 0, road_odd_tileno = 0, i;

      for (i = 0; i < t->num_valid_tileset_dirs / 2; i++) {
	enum direction8 even = t->valid_tileset_dirs[2 * i];
	enum direction8 odd = t->valid_tileset_dirs[2 * i + 1];

	if (draw_road[even]) {
	  road_even_tileno |= 1 << i;
	}
	if (draw_road[odd]) {
	  road_odd_tileno |= 1 << i;
	}
      }

      /* Draw the cardinal/even roads first. */
      if (road_even_tileno != 0) {
	ADD_SPRITE_SIMPLE(t->sprites.roads[road_idx].u.combo.even[road_even_tileno]);
      }
      if (road_odd_tileno != 0) {
	ADD_SPRITE_SIMPLE(t->sprites.roads[road_idx].u.combo.odd[road_odd_tileno]);
      }
    }
  } else if (roadstyle == RSTYLE_ALL_COMBINED) {
    /* RSTYLE_ALL_COMBINED is a very simple method that lets us simply retrieve 
     * entire finished tiles, with a bitwise index of the presence of
     * roads in each direction. */

    /* Draw roads first */
    if (road) {
      int road_tileno = 0, i;

      for (i = 0; i < t->num_valid_tileset_dirs; i++) {
	enum direction8 dir = t->valid_tileset_dirs[i];

	if (draw_road[dir]) {
	  road_tileno |= 1 << i;
	}
      }

      if (road_tileno != 0 || draw_single_road) {
        ADD_SPRITE_SIMPLE(t->sprites.roads[road_idx].u.total[road_tileno]);
      }
    }
  } else {
    fc_assert(FALSE);
  }

  /* Draw isolated rail/road separately (RSTYLE_ALL_SEPARATE and
     RSTYLE_PARITY_COMBINED only). */
  if (roadstyle == RSTYLE_ALL_SEPARATE || roadstyle == RSTYLE_PARITY_COMBINED) { 
    if (draw_single_road) {
      ADD_SPRITE_SIMPLE(t->sprites.roads[road_idx].isolated);
    }
  }

  return sprs - saved_sprs;
}

/**************************************************************************
  Return the index of the sprite to be used for irrigation or farmland in
  this tile.

  We assume that the current tile has farmland or irrigation.  We then
  choose a sprite (index) based upon which cardinally adjacent tiles have
  either farmland or irrigation (the two are considered interchangable for
  this).
**************************************************************************/
static int get_irrigation_index(const struct tileset *t,
				bv_special *tspecial_near)
{
  int tileno = 0, i;

  for (i = 0; i < t->num_cardinal_tileset_dirs; i++) {
    enum direction8 dir = t->cardinal_tileset_dirs[i];

    /* A tile with S_FARMLAND will also have S_IRRIGATION set. */
    if (contains_special(tspecial_near[dir], S_IRRIGATION)) {
      tileno |= 1 << i;
    }
  }

  return tileno;
}

/**************************************************************************
  Fill in the farmland/irrigation sprite for the tile.
**************************************************************************/
static int fill_irrigation_sprite_array(const struct tileset *t,
					struct drawn_sprite *sprs,
					bv_special tspecial,
					bv_special *tspecial_near,
					const struct city *pcity)
{
  struct drawn_sprite *saved_sprs = sprs;

  /* Tiles with S_FARMLAND also have S_IRRIGATION set. */
  fc_assert_ret_val(!contains_special(tspecial, S_FARMLAND)
                    || contains_special(tspecial, S_IRRIGATION), 0);

  /* We don't draw the irrigation if there's a city (it just gets overdrawn
   * anyway, and ends up looking bad). */
  if (draw_irrigation
      && contains_special(tspecial, S_IRRIGATION)
      && !(pcity && draw_cities)) {
    int index = get_irrigation_index(t, tspecial_near);

    if (contains_special(tspecial, S_FARMLAND)) {
      ADD_SPRITE_SIMPLE(t->sprites.tx.farmland[index]);
    } else {
      ADD_SPRITE_SIMPLE(t->sprites.tx.irrigation[index]);
    }
  }

  return sprs - saved_sprs;
}

/**************************************************************************
  Fill in the city overlays for the tile.  This includes the citymap
  overlays on the mapview as well as the tile output sprites.
**************************************************************************/
static int fill_city_overlays_sprite_array(const struct tileset *t,
					   struct drawn_sprite *sprs,
					   const struct tile *ptile,
					   const struct city *citymode)
{
  const struct city *pcity;
  const struct city *pwork;
  struct unit *psettler;
  struct drawn_sprite *saved_sprs = sprs;
  int city_x, city_y;
  const int NUM_CITY_COLORS = t->sprites.city.worked_tile_overlay.size;

  if (NULL == ptile || TILE_UNKNOWN == client_tile_get_known(ptile)) {
    return 0;
  }
  pwork = tile_worked(ptile);

  if (citymode) {
    pcity = citymode;
  } else {
    pcity = find_city_or_settler_near_tile(ptile, &psettler);
  }

  /* Below code does not work if pcity is invisible.
   * Make sure it is not. */
  fc_assert_ret_val(pcity == NULL || pcity->tile != NULL, 0);
  if (pcity && !pcity->tile) {
    pcity = NULL;
  }

  if (pcity && city_base_to_city_map(&city_x, &city_y, pcity, ptile)) {
    /* FIXME: check elsewhere for valid tile (instead of above) */

    if (!citymode && pcity->client.colored) {
      /* Add citymap overlay for a city. */
      int index = pcity->client.color_index % NUM_CITY_COLORS;

      if (NULL != pwork && pwork == pcity) {
        ADD_SPRITE_SIMPLE(t->sprites.city.worked_tile_overlay.p[index]);
      } else if (city_can_work_tile(pcity, ptile)) {
        ADD_SPRITE_SIMPLE(t->sprites.city.unworked_tile_overlay.p[index]);
      }
    } else if (NULL != pwork && pwork == pcity
               && (citymode || draw_city_output)) {
      /* Add on the tile output sprites. */
      int food = city_tile_output_now(pcity, ptile, O_FOOD);
      int shields = city_tile_output_now(pcity, ptile, O_SHIELD);
      int trade = city_tile_output_now(pcity, ptile, O_TRADE);
      const int ox = t->type == TS_ISOMETRIC ? t->normal_tile_width / 3 : 0;
      const int oy = t->type == TS_ISOMETRIC ? -t->normal_tile_height / 3 : 0;

      food = CLIP(0, food, NUM_TILES_DIGITS - 1);
      shields = CLIP(0, shields, NUM_TILES_DIGITS - 1);
      trade = CLIP(0, trade, NUM_TILES_DIGITS - 1);

      ADD_SPRITE(t->sprites.city.tile_foodnum[food], TRUE, ox, oy);
      ADD_SPRITE(t->sprites.city.tile_shieldnum[shields], TRUE, ox, oy);
      ADD_SPRITE(t->sprites.city.tile_tradenum[trade], TRUE, ox, oy);
    }
  } else if (psettler && psettler->client.colored) {
    /* Add citymap overlay for a unit. */
    int index = psettler->client.color_index % NUM_CITY_COLORS;

    ADD_SPRITE_SIMPLE(t->sprites.city.unworked_tile_overlay.p[index]);
  }

  return sprs - saved_sprs;
}

/****************************************************************************
  Helper function for fill_terrain_sprite_layer.
  Fill in the sprite array for blended terrain.
****************************************************************************/
static int fill_terrain_sprite_blending(const struct tileset *t,
					struct drawn_sprite *sprs,
					const struct tile *ptile,
					const struct terrain *pterrain,
					struct terrain **tterrain_near)
{
  struct drawn_sprite *saved_sprs = sprs;
  const int W = t->normal_tile_width, H = t->normal_tile_height;
  const int offsets[4][2] = {
    {W/2, 0}, {0, H / 2}, {W / 2, H / 2}, {0, 0}
  };
  enum direction4 dir = 0;

  /*
   * We want to mark unknown tiles so that an unreal tile will be
   * given the same marking as our current tile - that way we won't
   * get the "unknown" dither along the edge of the map.
   */
  for (; dir < 4; dir++) {
    struct tile *tile1 = mapstep(ptile, DIR4_TO_DIR8[dir]);
    struct terrain *other;

    if (!tile1
	|| client_tile_get_known(tile1) == TILE_UNKNOWN
	|| pterrain == (other = tterrain_near[DIR4_TO_DIR8[dir]])
	|| (0 == t->sprites.drawing[terrain_index(other)]->blending
	   &&  NULL == t->sprites.drawing[terrain_index(other)]->blender)) {
      continue;
    }

    ADD_SPRITE(t->sprites.drawing[terrain_index(other)]->blend[dir], TRUE,
	       offsets[dir][0], offsets[dir][1]);
  }

  return sprs - saved_sprs;
}

/****************************************************************************
  Add sprites for fog (and some forms of darkness).
****************************************************************************/
static int fill_fog_sprite_array(const struct tileset *t,
				 struct drawn_sprite *sprs,
				 const struct tile *ptile,
				 const struct tile_edge *pedge,
				 const struct tile_corner *pcorner)
{
  struct drawn_sprite *saved_sprs = sprs;

  if (t->fogstyle == FOG_SPRITE && draw_fog_of_war
      && NULL != ptile
      && TILE_KNOWN_UNSEEN == client_tile_get_known(ptile)) {
    /* With FOG_AUTO, fog is done this way. */
    ADD_SPRITE_SIMPLE(t->sprites.tx.fog);
  }

  if (t->darkness_style == DARKNESS_CORNER && pcorner && draw_fog_of_war) {
    int i, tileno = 0;

    for (i = 3; i >= 0; i--) {
      const int unknown = 0, fogged = 1, known = 2;
      int value = -1;

      if (!pcorner->tile[i]) {
	value = fogged;
      } else {
	switch (client_tile_get_known(pcorner->tile[i])) {
	case TILE_KNOWN_SEEN:
	  value = known;
	  break;
	case TILE_KNOWN_UNSEEN:
	  value = fogged;
	  break;
	case TILE_UNKNOWN:
	  value = unknown;
	  break;
	}
      }
      fc_assert(value >= 0 && value < 3);

      tileno = tileno * 3 + value;
    }

    if (t->sprites.tx.fullfog[tileno]) {
      ADD_SPRITE_SIMPLE(t->sprites.tx.fullfog[tileno]);
    }
  }

  return sprs - saved_sprs;
}

/****************************************************************************
  Helper function for fill_terrain_sprite_layer.
****************************************************************************/
static int fill_terrain_sprite_array(struct tileset *t,
				     struct drawn_sprite *sprs,
				     int l, /* layer_num */
				     const struct tile *ptile,
				     const struct terrain *pterrain,
				     struct terrain **tterrain_near,
				     struct drawing_data *draw)
{
  struct drawn_sprite *saved_sprs = sprs;
  struct drawing_layer *dlp = &draw->layer[l];
  int this = dlp->match_index[0];
  int that = dlp->match_index[1];
  int ox = dlp->offset_x;
  int oy = dlp->offset_y;
  int i;

#define MATCH(dir)							    \
    (t->sprites.drawing[terrain_index(tterrain_near[(dir)])]->num_layers > l	    \
     ? t->sprites.drawing[terrain_index(tterrain_near[(dir)])]->layer[l].match_index[0] \
     : -1)

  switch (dlp->sprite_type) {
  case CELL_WHOLE:
    {
      switch (dlp->match_style) {
      case MATCH_NONE:
	{
	  int count = sprite_vector_size(&dlp->base);

	  if (count > 0) {
            /* Pseudo-random reproducable algorithm to pick a sprite. Use modulo
             * to limit the number to a handleable size [0..32000). */
            count = fc_randomly(tile_index(ptile) % 32000, count);

	    if (dlp->is_tall) {
	      ox += FULL_TILE_X_OFFSET;
	      oy += FULL_TILE_Y_OFFSET;
	    }
	    ADD_SPRITE(dlp->base.p[count], TRUE, ox, oy);
	  }
	  break;
	}
      case MATCH_SAME:
	{
	  int tileno = 0;

	  for (i = 0; i < t->num_cardinal_tileset_dirs; i++) {
	    enum direction8 dir = t->cardinal_tileset_dirs[i];

	    if (MATCH(dir) == this) {
	      tileno |= 1 << i;
	    }
	  }

	  if (dlp->is_tall) {
	    ox += FULL_TILE_X_OFFSET;
	    oy += FULL_TILE_Y_OFFSET;
	  }
	  ADD_SPRITE(dlp->match[tileno], TRUE, ox, oy);
	  break;
	}
      case MATCH_PAIR:
      case MATCH_FULL:
        fc_assert(FALSE); /* not yet defined */
        break;
      };
      break;
    }
  case CELL_CORNER:
    {
      /* Divide the tile up into four rectangular cells.  Each of these
       * cells covers one corner, and each is adjacent to 3 different
       * tiles.  For each cell we pick a sprite based upon the adjacent
       * terrains at each of those tiles.  Thus, we have 8 different sprites
       * for each of the 4 cells (32 sprites total).
       *
       * These arrays correspond to the direction4 ordering. */
      const int W = t->normal_tile_width;
      const int H = t->normal_tile_height;
      const int iso_offsets[4][2] = {
	{W / 4, 0}, {W / 4, H / 2}, {W / 2, H / 4}, {0, H / 4}
      };
      const int noniso_offsets[4][2] = {
	{0, 0}, {W / 2, H / 2}, {W / 2, 0}, {0, H / 2}
      };

      /* put corner cells */
      for (i = 0; i < NUM_CORNER_DIRS; i++) {
	const int count = dlp->match_indices;
	int array_index = 0;
	enum direction8 dir = dir_ccw(DIR4_TO_DIR8[i]);
	int x = (t->type == TS_ISOMETRIC ? iso_offsets[i][0] : noniso_offsets[i][0]);
	int y = (t->type == TS_ISOMETRIC ? iso_offsets[i][1] : noniso_offsets[i][1]);
	int m[3] = {MATCH(dir_ccw(dir)), MATCH(dir), MATCH(dir_cw(dir))};
	struct sprite *s;

	/* synthesize 4 dimensional array? */
	switch (dlp->match_style) {
	case MATCH_NONE:
	  /* We have no need for matching, just plug the piece in place. */
	  break;
	case MATCH_SAME:
	  array_index = array_index * 2 + (m[2] != this);
	  array_index = array_index * 2 + (m[1] != this);
	  array_index = array_index * 2 + (m[0] != this);
	  break;
	case MATCH_PAIR:
	  array_index = array_index * 2 + (m[2] == that);
	  array_index = array_index * 2 + (m[1] == that);
	  array_index = array_index * 2 + (m[0] == that);
	  break;
	case MATCH_FULL:
	default:
	  {
	    int n[3];
	    int j = 0;
	    for (; j < 3; j++) {
	      int k = 0;
	      for (; k < count; k++) {
		n[j] = k; /* default to last entry */
		if (m[j] == dlp->match_index[k])
		{
		  break;
		}
	      }
	    }
	    array_index = array_index * count + n[2];
	    array_index = array_index * count + n[1];
	    array_index = array_index * count + n[0];
	  }
	  break;
	};
	array_index = array_index * NUM_CORNER_DIRS + i;

	s = dlp->cells[array_index];
	if (s) {
	  ADD_SPRITE(s, TRUE, x, y);
	}
      }
      break;
    }
  };
#undef MATCH

  return sprs - saved_sprs;
}

/****************************************************************************
  Helper function for fill_terrain_sprite_layer.
  Fill in the sprite array of darkness.
****************************************************************************/
static int fill_terrain_sprite_darkness(struct tileset *t,
				     struct drawn_sprite *sprs,
				     const struct tile *ptile,
				     struct terrain **tterrain_near)
{
  struct drawn_sprite *saved_sprs = sprs;
  int i, tileno;
  struct tile *adjc_tile;

#define UNKNOWN(dir)                                        \
    ((adjc_tile = mapstep(ptile, (dir)))		    \
     && client_tile_get_known(adjc_tile) == TILE_UNKNOWN)

  switch (t->darkness_style) {
  case DARKNESS_NONE:
    break;
  case DARKNESS_ISORECT:
    for (i = 0; i < 4; i++) {
      const int W = t->normal_tile_width, H = t->normal_tile_height;
      int offsets[4][2] = {{W / 2, 0}, {0, H / 2}, {W / 2, H / 2}, {0, 0}};

      if (UNKNOWN(DIR4_TO_DIR8[i])) {
	ADD_SPRITE(t->sprites.tx.darkness[i], TRUE,
		   offsets[i][0], offsets[i][1]);
      }
    }
    break;
  case DARKNESS_CARD_SINGLE:
    for (i = 0; i < t->num_cardinal_tileset_dirs; i++) {
      if (UNKNOWN(t->cardinal_tileset_dirs[i])) {
	ADD_SPRITE_SIMPLE(t->sprites.tx.darkness[i]);
      }
    }
    break;
  case DARKNESS_CARD_FULL:
    /* We're looking to find the INDEX_NSEW for the directions that
     * are unknown.  We want to mark unknown tiles so that an unreal
     * tile will be given the same marking as our current tile - that
     * way we won't get the "unknown" dither along the edge of the
     * map. */
    tileno = 0;
    for (i = 0; i < t->num_cardinal_tileset_dirs; i++) {
      if (UNKNOWN(t->cardinal_tileset_dirs[i])) {
	tileno |= 1 << i;
      }
    }

    if (tileno != 0) {
      ADD_SPRITE_SIMPLE(t->sprites.tx.darkness[tileno]);
    }
    break;
  case DARKNESS_CORNER:
    /* Handled separately. */
    break;
  };
#undef UNKNOWN

  return sprs - saved_sprs;
}

/****************************************************************************
  Add sprites for the base tile to the sprite list.  This doesn't
  include specials or rivers.
****************************************************************************/
static int fill_terrain_sprite_layer(struct tileset *t,
				     struct drawn_sprite *sprs,
				     int layer_num,
				     const struct tile *ptile,
				     const struct terrain *pterrain,
				     struct terrain **tterrain_near)
{
  struct sprite *sprite;
  struct drawn_sprite *saved_sprs = sprs;
  struct drawing_data *draw = t->sprites.drawing[terrain_index(pterrain)];
  const int l = (draw->is_reversed
		 ? (draw->num_layers - layer_num - 1) : layer_num);

  /* Skip the normal drawing process. */
  /* FIXME: this should avoid calling load_sprite since it's slow and
   * increases the refcount without limit. */
  if (ptile->spec_sprite && (sprite = load_sprite(t, ptile->spec_sprite))) {
    if (l == 0) {
      ADD_SPRITE_SIMPLE(sprite);
      return 1;
    } else {
      return 0;
    }
  }

  if (l < draw->num_layers) {
    sprs += fill_terrain_sprite_array(t, sprs, l, ptile, pterrain, tterrain_near, draw);

    if ((l + 1) == draw->blending) {
      sprs += fill_terrain_sprite_blending(t, sprs, ptile, pterrain, tterrain_near);
    }
  }

  /* Add darkness on top of the first layer.  Note that darkness is always
   * drawn, even in citymode, etc. */
  if (l == 0) {
    sprs += fill_terrain_sprite_darkness(t, sprs, ptile, tterrain_near);
  }

  return sprs - saved_sprs;
}


/****************************************************************************
  Fill in the grid sprites for the given tile, city, and unit.
****************************************************************************/
static int fill_grid_sprite_array(const struct tileset *t,
				  struct drawn_sprite *sprs,
				  const struct tile *ptile,
				  const struct tile_edge *pedge,
				  const struct tile_corner *pcorner,
				  const struct unit *punit,
				  const struct city *pcity,
				  const struct city *citymode)
{
  struct drawn_sprite *saved_sprs = sprs;

  if (pedge) {
    bool known[NUM_EDGE_TILES], city[NUM_EDGE_TILES];
    bool unit[NUM_EDGE_TILES], worked[NUM_EDGE_TILES];
    int i;
    struct unit_list *pfocus_units = get_units_in_focus();

    for (i = 0; i < NUM_EDGE_TILES; i++) {
      int dummy_x, dummy_y;
      const struct tile *tile = pedge->tile[i];
      struct player *powner = tile ? tile_owner(tile) : NULL;

      known[i] = tile && client_tile_get_known(tile) != TILE_UNKNOWN;
      unit[i] = FALSE;
      if (tile) {
        unit_list_iterate(pfocus_units, pfocus_unit) {
          if (unit_has_type_flag(pfocus_unit, UTYF_CITIES)
              && !unit_has_orders(pfocus_unit)
              && city_can_be_built_here(unit_tile(pfocus_unit), pfocus_unit)
              && city_tile_to_city_map(&dummy_x, &dummy_y,
                                       game.info.init_city_radius_sq,
                                       unit_tile(pfocus_unit), tile)) {
            unit[i] = TRUE;
            break;
          }
        } unit_list_iterate_end;
      }
      worked[i] = FALSE;

      city[i] = (tile
                 && (NULL == powner || NULL == client.conn.playing
                     || powner == client.conn.playing)
                 && (NULL == client.conn.playing
                     || player_in_city_map(client.conn.playing, tile)));
      if (city[i]) {
	if (citymode) {
	  /* In citymode, we only draw worked tiles for this city - other
	   * tiles may be marked as unavailable. */
	  worked[i] = (tile_worked(tile) == citymode);
	} else {
	  worked[i] = (NULL != tile_worked(tile));
	}
      }
    }

    if (mapdeco_is_highlight_set(pedge->tile[0])
        || mapdeco_is_highlight_set(pedge->tile[1])) {
      ADD_SPRITE_SIMPLE(t->sprites.grid.selected[pedge->type]);
    } else if (!draw_terrain && draw_coastline
	       && pedge->tile[0] && pedge->tile[1]
	       && known[0] && known[1]
	       && (is_ocean_tile(pedge->tile[0])
		   ^ is_ocean_tile(pedge->tile[1]))) {
      ADD_SPRITE_SIMPLE(t->sprites.grid.coastline[pedge->type]);
    } else if (draw_map_grid) {
      if (worked[0] || worked[1]) {
	ADD_SPRITE_SIMPLE(t->sprites.grid.worked[pedge->type]);
      } else if (city[0] || city[1]) {
	ADD_SPRITE_SIMPLE(t->sprites.grid.city[pedge->type]);
      } else if (known[0] || known[1]) {
	ADD_SPRITE_SIMPLE(t->sprites.grid.main[pedge->type]);
      }
    } else if (draw_city_outlines) {
      if (XOR(city[0], city[1])) {
	ADD_SPRITE_SIMPLE(t->sprites.grid.city[pedge->type]);
      }
      if (!citymode && XOR(unit[0], unit[1])) {
	ADD_SPRITE_SIMPLE(t->sprites.grid.worked[pedge->type]);
      }
    }

    if (draw_borders
        && BORDERS_DISABLED != game.info.borders
        && known[0]
        && known[1]) {
      struct player *owner0 = tile_owner(pedge->tile[0]);
      struct player *owner1 = tile_owner(pedge->tile[1]);

      if (owner0 != owner1) {
        if (owner0) {
          int plrid = player_index(owner0);
          ADD_SPRITE_SIMPLE(t->sprites.player[plrid].grid_borders
                            [pedge->type][0]);
        }
        if (owner1) {
          int plrid = player_index(owner1);
          ADD_SPRITE_SIMPLE(t->sprites.player[plrid].grid_borders
                            [pedge->type][1]);
        }
      }
    }
  } else if (NULL != ptile && TILE_UNKNOWN != client_tile_get_known(ptile)) {
    int cx, cy;

    if (citymode
        /* test to ensure valid coordinates? */
        && city_base_to_city_map(&cx, &cy, citymode, ptile)
        && !client_city_can_work_tile(citymode, ptile)) {
      ADD_SPRITE_SIMPLE(t->sprites.grid.unavailable);
    }

    if (draw_native && citymode == NULL) {
      bool native = TRUE;
      struct unit_list *pfocus_units = get_units_in_focus();

      unit_list_iterate(pfocus_units, punit) {
        if (!is_native_tile(unit_type(punit), ptile)) {
          native = FALSE;
          break;
        }
      } unit_list_iterate_end;

      if (!native) {
        if (t->sprites.grid.nonnative != NULL) {
          ADD_SPRITE_SIMPLE(t->sprites.grid.nonnative);
        } else {
          ADD_SPRITE_SIMPLE(t->sprites.grid.unavailable);
        }
      }
    }
  }

  return sprs - saved_sprs;
}

/****************************************************************************
  Fill in the given sprite array with any needed goto sprites.
****************************************************************************/
static int fill_goto_sprite_array(const struct tileset *t,
				  struct drawn_sprite *sprs,
				  const struct tile *ptile,
				  const struct tile_edge *pedge,
				  const struct tile_corner *pcorner)
{
  struct drawn_sprite *saved_sprs = sprs;

  if (is_valid_goto_destination(ptile)) {
    bool warn= FALSE;
    int length;

    goto_get_turns(NULL, &length);

    if (0 > length) {
      ADD_SPRITE_SIMPLE(t->sprites.path.turns[0]);
      warn = TRUE;
    } else {
      ADD_SPRITE_SIMPLE(t->sprites.path.turns[length % 10]);
      if (10 <= length) {
        ADD_SPRITE_SIMPLE(t->sprites.path.turns_tens[(length / 10) % 10]);
        if (100 <= length) {
          struct sprite *sprite =
              t->sprites.path.turns_hundreds[(length / 100) % 10];

          if (NULL != sprite) {
            ADD_SPRITE_SIMPLE(sprite);
          } else {
            warn = TRUE;
          }
          if (1000 <= length) {
            warn = TRUE;
          }
        }
      }
    }

    if (warn) {
      /* Warn only once by tileset. */
      static char last_reported[256] = "";

      if (0 != strcmp(last_reported, t->name)) {
        log_normal(_("Tileset \"%s\" doesn't support long goto paths, "
                     "such as %d. Path not displayed as expected."),
                   t->name, length);
        sz_strlcpy(last_reported, t->name);
      }
    }
  }

  return sprs - saved_sprs;
}

/****************************************************************************
  Fill in the sprite array for the given tile, city, and unit.

  ptile, if specified, gives the tile.  If specified the terrain and specials
  will be drawn for this tile.  In this case (map_x,map_y) should give the
  location of the tile.

  punit, if specified, gives the unit.  For tile drawing this should
  generally be get_drawable_unit(); otherwise it can be any unit.

  pcity, if specified, gives the city.  For tile drawing this should
  generally be tile_city(ptile); otherwise it can be any city.

  citymode specifies whether this is part of a citydlg.  If so some drawing
  is done differently.
****************************************************************************/
int fill_sprite_array(struct tileset *t,
                      struct drawn_sprite *sprs, enum mapview_layer layer,
                      const struct tile *ptile,
                      const struct tile_edge *pedge,
                      const struct tile_corner *pcorner,
                      const struct unit *punit, const struct city *pcity,
                      const struct city *citymode,
                      const struct unit_type *putype)
{
  int tileno, dir;
  bv_special tspecial_near[8];
  bv_special tspecial;
  bv_roads troad;
  bv_roads troad_near[8];
  struct terrain *tterrain_near[8];
  struct terrain *pterrain = NULL;
  struct drawn_sprite *save_sprs = sprs;
  struct player *owner = NULL;
  /* Unit drawing is disabled when the view options are turned off,
   * but only where we're drawing on the mapview. */
  bool do_draw_unit = (punit && (draw_units || !ptile
				 || (draw_focus_unit
				     && unit_is_in_focus(punit))));
  bool solid_bg = (solid_color_behind_units
		   && (do_draw_unit
		       || (pcity && draw_cities)
		       || (ptile && !draw_terrain)));

  if (citymode) {
    int count = 0, i, cx, cy;
    const struct tile *const *tiles = NULL;
    bool valid = FALSE;

    if (ptile) {
      tiles = &ptile;
      count = 1;
    } else if (pcorner) {
      tiles = pcorner->tile;
      count = NUM_CORNER_TILES;
    } else if (pedge) {
      tiles = pedge->tile;
      count = NUM_EDGE_TILES;
    }

    for (i = 0; i < count; i++) {
      if (tiles[i] && city_base_to_city_map(&cx, &cy, citymode, tiles[i])) {
	valid = TRUE;
	break;
      }
    }
    if (!valid) {
      return 0;
    }
  }

  if (ptile && client_tile_get_known(ptile) != TILE_UNKNOWN) {
    tspecial = tile_specials(ptile);
    troad = tile_roads(ptile);
    pterrain = tile_terrain(ptile);

    if (NULL != pterrain) {
      build_tile_data(ptile, pterrain, tterrain_near, tspecial_near,
                      troad_near);
    } else {
      log_error("fill_sprite_array() tile (%d,%d) has no terrain!",
                TILE_XY(ptile));
    }
  } else {
    BV_CLR_ALL(tspecial);
    BV_CLR_ALL(troad);
  }

  switch (layer) {
  case LAYER_BACKGROUND:
    /* Set up background color. */
    if (solid_color_behind_units) {
      if (do_draw_unit) {
	owner = unit_owner(punit);
      } else if (pcity && draw_cities) {
	owner = city_owner(pcity);
      }
    }
    if (owner) {
      ADD_SPRITE_SIMPLE(t->sprites.player[player_index(owner)].background);
    } else if (ptile && !draw_terrain) {
      ADD_SPRITE_SIMPLE(t->sprites.background.graphic);
    }
    break;

  case LAYER_TERRAIN1:
    if (NULL != pterrain && draw_terrain && !solid_bg) {
      sprs += fill_terrain_sprite_layer(t, sprs, 0, ptile, pterrain, tterrain_near);
    }
    break;

  case LAYER_TERRAIN2:
    if (NULL != pterrain && draw_terrain && !solid_bg) {
      sprs += fill_terrain_sprite_layer(t, sprs, 1, ptile, pterrain, tterrain_near);
    }
    break;

  case LAYER_TERRAIN3:
    if (NULL != pterrain && draw_terrain && !solid_bg) {
      fc_assert(MAX_NUM_LAYERS == 3);
      sprs += fill_terrain_sprite_layer(t, sprs, 2, ptile, pterrain, tterrain_near);
    }
    break;

  case LAYER_WATER:
    if (NULL != pterrain) {
      if (draw_terrain && !solid_bg
          && terrain_type_terrain_class(pterrain) == TC_OCEAN) {
	for (dir = 0; dir < 4; dir++) {
          int didx = DIR4_TO_DIR8[dir];

	  if (contains_special(tspecial_near[didx], S_RIVER)) {
            ADD_SPRITE_SIMPLE(t->sprites.tx.rivers.outlet[dir]);
	  }

          road_type_list_iterate(t->rivers, priver) {
            int idx = road_index(priver);

            if (BV_ISSET(troad_near[didx], idx)) {
              ADD_SPRITE_SIMPLE(t->sprites.roads[idx].u.rivers.outlet[dir]);
              break;
            }
          } road_type_list_iterate_end;
	}
      }

      sprs += fill_irrigation_sprite_array(t, sprs, tspecial, tspecial_near,
					   pcity);

      if (draw_terrain && !solid_bg) {
        if (contains_special(tspecial, S_RIVER)) {
          int i;

          /* Draw rivers on top of irrigation. */
          tileno = 0;
          for (i = 0; i < t->num_cardinal_tileset_dirs; i++) {
            enum direction8 dir = t->cardinal_tileset_dirs[i];

            if (terrain_type_terrain_class(tterrain_near[dir]) == TC_OCEAN
                || contains_special(tspecial_near[dir], S_RIVER)) {
              tileno |= 1 << i;
            }
          }
          ADD_SPRITE_SIMPLE(t->sprites.tx.rivers.spec[tileno]);
        }

        road_type_list_iterate(t->rivers, priver) {
          int idx = road_index(priver);

          if (BV_ISSET(troad, idx)) {
            int i;

            /* Draw rivers on top of irrigation. */
            tileno = 0;
            for (i = 0; i < t->num_cardinal_tileset_dirs; i++) {
              enum direction8 dir = t->cardinal_tileset_dirs[i];

              if (terrain_type_terrain_class(tterrain_near[dir]) == TC_OCEAN
                  || BV_ISSET(troad_near[dir], idx)) {
                tileno |= 1 << i;
              }
            }

            ADD_SPRITE_SIMPLE(t->sprites.roads[idx].u.rivers.spec[tileno]);
          }
        } road_type_list_iterate_end;
      }
    }
    break;

  case LAYER_ROADS:
    if (NULL != pterrain) {
      road_type_iterate(proad) {
        sprs += fill_road_sprite_array(t, proad, sprs,
                                       troad, troad_near,
                                       tterrain_near, pcity);
      } road_type_iterate_end;
    }
    break;

  case LAYER_SPECIAL1:
    if (NULL != pterrain) {
      if (draw_specials) {
	if (tile_resource_is_valid(ptile)) {
	  ADD_SPRITE_SIMPLE(t->sprites.resource[resource_index(tile_resource(ptile))]);
	}
      }

      if (ptile && draw_fortress_airbase) {
        base_type_iterate(pbase) {
          if (tile_has_base(ptile, pbase)
              && t->sprites.bases[base_index(pbase)].background) {
            ADD_SPRITE_FULL(t->sprites.bases[base_index(pbase)].background);
          }
        } base_type_iterate_end;
      }

      if (draw_mines && contains_special(tspecial, S_MINE)) {
        struct drawing_data *draw = t->sprites.drawing[terrain_index(pterrain)];

        if (NULL != draw->mine) {
	  ADD_SPRITE_SIMPLE(draw->mine);
	}
      }

      if (draw_specials && contains_special(tspecial, S_HUT)) {
	ADD_SPRITE_SIMPLE(t->sprites.tx.village);
      }
    }
    break;

  case LAYER_GRID1:
    if (t->type == TS_ISOMETRIC) {
      sprs += fill_grid_sprite_array(t, sprs, ptile, pedge, pcorner,
				     punit, pcity, citymode);
    }
    break;

  case LAYER_CITY1:
    /* City.  Some city sprites are drawn later. */
    if (pcity && draw_cities) {
      if (!draw_full_citybar && !solid_color_behind_units) {
	ADD_SPRITE(get_city_flag_sprite(t, pcity), TRUE,
		   FULL_TILE_X_OFFSET + t->city_flag_offset_x,
		   FULL_TILE_Y_OFFSET + t->city_flag_offset_y);
      }
      /* For iso-view the city.wall graphics include the full city, whereas
       * for non-iso view they are an overlay on top of the base city
       * graphic. */
      if (t->type == TS_OVERVIEW || !pcity->client.walls) {
	ADD_SPRITE_FULL(get_city_sprite(t->sprites.city.tile, pcity));
      }
      if (t->type == TS_ISOMETRIC && pcity->client.walls) {
	ADD_SPRITE_FULL(get_city_sprite(t->sprites.city.wall, pcity));
      }
      if (!draw_full_citybar && pcity->client.occupied) {
	ADD_SPRITE_FULL(get_city_sprite(t->sprites.city.occupied, pcity));
      }
      if (t->type == TS_OVERVIEW && pcity->client.walls) {
	ADD_SPRITE_FULL(get_city_sprite(t->sprites.city.wall, pcity));
      }
      if (pcity->client.unhappy) {
	ADD_SPRITE_FULL(t->sprites.city.disorder);
      }
    }
    break;

  case LAYER_SPECIAL2:
    if (NULL != pterrain) {
      if (ptile && draw_fortress_airbase) {
        base_type_iterate(pbase) {
          if (tile_has_base(ptile, pbase)
              && t->sprites.bases[base_index(pbase)].middleground) {
            ADD_SPRITE_FULL(t->sprites.bases[base_index(pbase)].middleground);
          }
        } base_type_iterate_end;
      }

      if (draw_pollution && contains_special(tspecial, S_POLLUTION)) {
	ADD_SPRITE_SIMPLE(t->sprites.tx.pollution);
      }
      if (draw_pollution && contains_special(tspecial, S_FALLOUT)) {
	ADD_SPRITE_SIMPLE(t->sprites.tx.fallout);
      }
    }
    break;

  case LAYER_UNIT:
  case LAYER_FOCUS_UNIT:
    if (do_draw_unit && XOR(layer == LAYER_UNIT, unit_is_in_focus(punit))) {
      bool stacked = ptile && (unit_list_size(ptile->units) > 1);
      bool backdrop = !pcity;

      if (ptile && unit_is_in_focus(punit)
	  && t->sprites.unit.select[0]) {
	/* Special case for drawing the selection rectangle.  The blinking
	 * unit is handled separately, inside get_drawable_unit(). */
	ADD_SPRITE_SIMPLE(t->sprites.unit.select[focus_unit_state]);
      }

      sprs += fill_unit_sprite_array(t, sprs, punit, stacked, backdrop);
    } else if (putype != NULL) {
      /* Only the sprite for the unit type. */
      sprs += fill_unit_type_sprite_array(t, sprs, putype,
                                          direction8_invalid());
    }
    break;

  case LAYER_SPECIAL3:
    if (NULL != pterrain) {
      if (ptile && draw_fortress_airbase) {
        bool show_flag = FALSE;
        struct player *owner = base_owner(ptile);

        base_type_iterate(pbase) {
          if (tile_has_base(ptile, pbase)) {
            if (t->sprites.bases[base_index(pbase)].foreground) {
              /* Draw fortress front in iso-view (non-iso view only has a fortress
               * back). */
              ADD_SPRITE_FULL(t->sprites.bases[base_index(pbase)].foreground);
            }
            if (base_has_flag(pbase, BF_SHOW_FLAG)) {
              show_flag = TRUE;
            }
          }
        } base_type_iterate_end;

        if (show_flag && owner != NULL) {
          ADD_SPRITE(get_nation_flag_sprite(t, nation_of_player(owner)), TRUE,
                     FULL_TILE_X_OFFSET + t->city_flag_offset_x,
                     FULL_TILE_Y_OFFSET + t->city_flag_offset_y);
        }
      }
    }
    break;

  case LAYER_FOG:
    sprs += fill_fog_sprite_array(t, sprs, ptile, pedge, pcorner);
    break;

  case LAYER_CITY2:
    /* City size.  Drawing this under fog makes it hard to read. */
    if (pcity && draw_cities && !draw_full_citybar) {
      bool warn = FALSE;

      ADD_SPRITE(t->sprites.city.size[city_size_get(pcity) % 10],
                 FALSE, FULL_TILE_X_OFFSET, FULL_TILE_Y_OFFSET);
      if (10 <= city_size_get(pcity)) {
        ADD_SPRITE(t->sprites.city.size_tens[(city_size_get(pcity) / 10)
                   % 10], FALSE, FULL_TILE_X_OFFSET, FULL_TILE_Y_OFFSET);
        if (100 <= city_size_get(pcity)) {
          struct sprite *sprite =
              t->sprites.city.size_hundreds[(city_size_get(pcity) / 100) % 10];

          if (NULL != sprite) {
            ADD_SPRITE(sprite, FALSE,
                       FULL_TILE_X_OFFSET, FULL_TILE_Y_OFFSET);
          } else {
            warn = TRUE;
          }
          if (1000 <= city_size_get(pcity)) {
            warn = TRUE;
          }
        }
      }

      if (warn) {
        /* Warn only once by tileset. */
        static char last_reported[256] = "";

        if (0 != strcmp(last_reported, t->name)) {
          log_normal(_("Tileset \"%s\" doesn't support big cities size, "
                       "such as %d. Size not displayed as expected."),
                     t->name, city_size_get(pcity));
          sz_strlcpy(last_reported, t->name);
        }
      }
    }
    break;

  case LAYER_GRID2:
    if (t->type == TS_OVERVIEW) {
      sprs += fill_grid_sprite_array(t, sprs, ptile, pedge, pcorner,
				     punit, pcity, citymode);
    }
    break;

  case LAYER_OVERLAYS:
    sprs += fill_city_overlays_sprite_array(t, sprs, ptile, citymode);
    if (mapdeco_is_crosshair_set(ptile)) {
      ADD_SPRITE_SIMPLE(t->sprites.user.attention);
    }
    break;

  case LAYER_CITYBAR:
  case LAYER_TILELABEL:
    /* Nothing.  This is just a placeholder. */
    break;

  case LAYER_GOTO:
    if (ptile && goto_is_active()) {
      sprs += fill_goto_sprite_array(t, sprs, ptile, pedge, pcorner);
    }
    break;

  case LAYER_EDITOR:
    if (ptile && editor_is_active()) {
      if (editor_tile_is_selected(ptile)) {
        int color = 2 % tileset_num_city_colors(tileset);
        ADD_SPRITE_SIMPLE(t->sprites.city.unworked_tile_overlay.p[color]);
      }

      if (NULL != map_startpos_get(ptile)) {
        /* FIXME: Use a more representative sprite. */
        ADD_SPRITE_SIMPLE(t->sprites.user.attention);
      }
    }
    break;

  case LAYER_COUNT:
    fc_assert(FALSE);
    break;
  }

  return sprs - save_sprs;
}

/**********************************************************************
  Set city tiles sprite values; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tileset_setup_city_tiles(struct tileset *t, int style)
{
  if (style == game.control.styles_count - 1) {

    /* Free old sprites */
    free_city_sprite(t->sprites.city.tile);
    free_city_sprite(t->sprites.city.wall);
    free_city_sprite(t->sprites.city.occupied);

    t->sprites.city.tile = load_city_sprite(t, "city");
    t->sprites.city.wall = load_city_sprite(t, "wall");
    t->sprites.city.occupied = load_city_sprite(t, "occupied");

    for (style = 0; style < game.control.styles_count; style++) {
      if (t->sprites.city.tile->styles[style].land_num_thresholds == 0) {
        log_fatal("City style \"%s\": no city graphics.",
                  city_style_rule_name(style));
        exit(EXIT_FAILURE);
      }
      if (t->sprites.city.wall->styles[style].land_num_thresholds == 0) {
        log_fatal("City style \"%s\": no wall graphics.",
                  city_style_rule_name(style));
        exit(EXIT_FAILURE);
      }
      if (t->sprites.city.occupied->styles[style].land_num_thresholds == 0) {
        log_fatal("City style \"%s\": no occupied graphics.",
                  city_style_rule_name(style));
        exit(EXIT_FAILURE);
      }
    }
  }
}

/****************************************************************************
  Return the amount of time between calls to toggle_focus_unit_state.
  The main loop needs to call toggle_focus_unit_state about this often
  to do the active-unit animation.
****************************************************************************/
double get_focus_unit_toggle_timeout(const struct tileset *t)
{
  if (t->sprites.unit.select[0]) {
    return 0.1;
  } else {
    return 0.5;
  }
}

/****************************************************************************
  Reset the focus unit state.  This should be called when changing
  focus units.
****************************************************************************/
void reset_focus_unit_state(struct tileset *t)
{
  focus_unit_state = 0;
}

/****************************************************************************
  Setup tileset for showing combat where focus unit participates.
****************************************************************************/
void focus_unit_in_combat(struct tileset *t)
{
  if (!t->sprites.unit.select[0]) {
    reset_focus_unit_state(t);
  }
}

/****************************************************************************
  Toggle/increment the focus unit state.  This should be called once
  every get_focus_unit_toggle_timeout() seconds.
****************************************************************************/
void toggle_focus_unit_state(struct tileset *t)
{
  focus_unit_state++;
  if (t->sprites.unit.select[0]) {
    focus_unit_state %= NUM_TILES_SELECT;
  } else {
    focus_unit_state %= 2;
  }
}

/**********************************************************************
  Find unit that we can display from given tile.
***********************************************************************/
struct unit *get_drawable_unit(const struct tileset *t,
			       struct tile *ptile,
			       const struct city *citymode)
{
  struct unit *punit = find_visible_unit(ptile);

  if (!punit)
    return NULL;

  if (citymode && unit_owner(punit) == city_owner(citymode))
    return NULL;

  if (!unit_is_in_focus(punit)
      || t->sprites.unit.select[0] || focus_unit_state == 0)
    return punit;
  else
    return NULL;
}

/****************************************************************************
  This patch unloads all sprites from the sprite hash (the hash itself
  is left intact).
****************************************************************************/
static void unload_all_sprites(struct tileset *t)
{
  if (t->sprite_hash) {
    sprite_hash_iterate(t->sprite_hash, tag_name, ss) {
      while (ss->ref_count > 0) {
        unload_sprite(t, tag_name);
      }
    } sprite_hash_iterate_end;
  }
}

/**********************************************************************
  Free all sprites from tileset.
***********************************************************************/
void tileset_free_tiles(struct tileset *t)
{
  log_debug("tileset_free_tiles()");

  unload_all_sprites(t);

  free_city_sprite(t->sprites.city.tile);
  t->sprites.city.tile = NULL;
  free_city_sprite(t->sprites.city.wall);
  t->sprites.city.wall = NULL;
  free_city_sprite(t->sprites.city.occupied);
  t->sprites.city.occupied = NULL;

  if (t->sprite_hash) {
    sprite_hash_destroy(t->sprite_hash);
    t->sprite_hash = NULL;
  }

  small_sprite_list_iterate(t->small_sprites, ss) {
    small_sprite_list_remove(t->small_sprites, ss);
    if (ss->file) {
      free(ss->file);
    }
    fc_assert(ss->sprite == NULL);
    free(ss);
  } small_sprite_list_iterate_end;

  specfile_list_iterate(t->specfiles, sf) {
    specfile_list_remove(t->specfiles, sf);
    free(sf->file_name);
    if (sf->big_sprite) {
      free_sprite(sf->big_sprite);
      sf->big_sprite = NULL;
    }
    free(sf);
  } specfile_list_iterate_end;

  sprite_vector_iterate(&t->sprites.city.worked_tile_overlay, psprite) {
    free_sprite(*psprite);
  } sprite_vector_iterate_end;
  sprite_vector_free(&t->sprites.city.worked_tile_overlay);

  sprite_vector_iterate(&t->sprites.city.unworked_tile_overlay, psprite) {
    free_sprite(*psprite);
  } sprite_vector_iterate_end;
  sprite_vector_free(&t->sprites.city.unworked_tile_overlay);

  if (t->sprites.tx.fullfog) {
    free(t->sprites.tx.fullfog);
    t->sprites.tx.fullfog = NULL;
  }

  sprite_vector_free(&t->sprites.colors.overlays);
  sprite_vector_free(&t->sprites.explode.unit);
  sprite_vector_free(&t->sprites.nation_flag);
  sprite_vector_free(&t->sprites.nation_shield);
  sprite_vector_free(&t->sprites.citybar.occupancy);

  tileset_background_free(t);
}

/**************************************************************************
  Return the sprite for drawing the given spaceship part.
**************************************************************************/
struct sprite *get_spaceship_sprite(const struct tileset *t,
				    enum spaceship_part part)
{
  return t->sprites.spaceship[part];
}

/**************************************************************************
  Return a sprite for the given citizen.  The citizen's type is given,
  as well as their index (in the range [0..city_size_get(pcity))).  The
  citizen's city can be used to determine which sprite to use (a NULL
  value indicates there is no city; i.e., the sprite is just being
  used as a picture).
**************************************************************************/
struct sprite *get_citizen_sprite(const struct tileset *t,
				  enum citizen_category type,
				  int citizen_index,
				  const struct city *pcity)
{
  const struct citizen_graphic *graphic;

  if (type < CITIZEN_SPECIALIST) {
    fc_assert(type >= 0);
    graphic = &t->sprites.citizen[type];
  } else {
    fc_assert(type < (CITIZEN_SPECIALIST + SP_MAX));
    graphic = &t->sprites.specialist[type - CITIZEN_SPECIALIST];
  }

  return graphic->sprite[citizen_index % graphic->count];
}

/**************************************************************************
  Return the sprite for the nation.
**************************************************************************/
struct sprite *get_nation_flag_sprite(const struct tileset *t,
				      const struct nation_type *pnation)
{
  return t->sprites.nation_flag.p[nation_index(pnation)];
}

/**************************************************************************
  Return the sprite for the technology/advance.
**************************************************************************/
struct sprite *get_tech_sprite(const struct tileset *t, Tech_type_id tech)
{
  fc_assert_ret_val(0 <= tech && tech < advance_count(), NULL);
  return t->sprites.tech[tech];
}

/**************************************************************************
  Return the sprite for the building/improvement.
**************************************************************************/
struct sprite *get_building_sprite(const struct tileset *t,
                                   struct impr_type *pimprove)
{
  fc_assert_ret_val(NULL != pimprove, NULL);
  return t->sprites.building[improvement_index(pimprove)];
}

/****************************************************************************
  Return the sprite for the government.
****************************************************************************/
struct sprite *get_government_sprite(const struct tileset *t,
                                     const struct government *gov)
{
  fc_assert_ret_val(NULL != gov, NULL);
  return t->sprites.government[government_index(gov)];
}

/****************************************************************************
  Return the sprite for the unit type (the base "unit" sprite).
****************************************************************************/
struct sprite *get_unittype_sprite(const struct tileset *t,
                                   const struct unit_type *punittype,
                                   enum direction8 facing)
{
  int uidx = utype_index(punittype);

  fc_assert_ret_val(NULL != punittype, NULL);

  if (t->sprites.units.icon[uidx]) {
    /* Has icon sprite */
    return t->sprites.units.icon[uidx];
  } else if (is_valid_dir(facing)) {
    return t->sprites.units.facing[uidx][facing];
  } else {
    /* Fallback to using random orientation sprite. */
    return t->sprites.units.facing[uidx][rand_direction()];
  }
}

/**************************************************************************
  Return a "sample" sprite for this city style.
**************************************************************************/
struct sprite *get_sample_city_sprite(const struct tileset *t,
				      int city_style)
{
  int num_thresholds =
    t->sprites.city.tile->styles[city_style].land_num_thresholds;

  if (num_thresholds == 0) {
    return NULL;
  } else {
    return (t->sprites.city.tile->styles[city_style]
	    .land_thresholds[num_thresholds - 1].sprite);
  }
}

/**************************************************************************
  Return a sprite with an "arrow" theme graphic.
**************************************************************************/
struct sprite *get_arrow_sprite(const struct tileset *t,
                                enum arrow_type arrow)
{
  fc_assert_ret_val(arrow >= 0 && arrow < ARROW_LAST, NULL);

  return t->sprites.arrow[arrow];
}

/**************************************************************************
  Return a tax sprite for the given output type (usually gold/lux/sci).
**************************************************************************/
struct sprite *get_tax_sprite(const struct tileset *t, Output_type_id otype)
{
  switch (otype) {
  case O_SCIENCE:
    return t->sprites.tax_science;
  case O_GOLD:
    return t->sprites.tax_gold;
  case O_LUXURY:
    return t->sprites.tax_luxury;
  case O_TRADE:
  case O_FOOD:
  case O_SHIELD:
  case O_LAST:
    break;
  }
  return NULL;
}

/**************************************************************************
  Return a thumbs-up/thumbs-down sprite to show treaty approval or
  disapproval.
**************************************************************************/
struct sprite *get_treaty_thumb_sprite(const struct tileset *t, bool on_off)
{
  return t->sprites.treaty_thumb[on_off ? 1 : 0];
}

/**************************************************************************
  Return a sprite_vector containing the animation sprites for a unit
  explosion.
**************************************************************************/
const struct sprite_vector *get_unit_explode_animation(const struct
						       tileset *t)
{
  return &t->sprites.explode.unit;
}

/****************************************************************************
  Return a sprite contining the single nuke graphic.

  TODO: This should be an animation like the unit explode animation.
****************************************************************************/
struct sprite *get_nuke_explode_sprite(const struct tileset *t)
{
  return t->sprites.explode.nuke;
}

/**************************************************************************
  Return all the sprites used for city bar drawing.
**************************************************************************/
const struct citybar_sprites *get_citybar_sprites(const struct tileset *t)
{
  if (draw_full_citybar) {
    return &t->sprites.citybar;
  } else {
    return NULL;
  }
}

/**************************************************************************
  Return all the sprites used for editor icons, images, etc.
**************************************************************************/
const struct editor_sprites *get_editor_sprites(const struct tileset *t)
{
  return &t->sprites.editor;
}

/**************************************************************************
  Returns a sprite for the given cursor.  The "hot" coordinates (the
  active coordinates of the mouse relative to the sprite) are placed int
  (*hot_x, *hot_y). 
  A cursor can consist of several frames to be used for animation.
**************************************************************************/
struct sprite *get_cursor_sprite(const struct tileset *t,
				 enum cursor_type cursor,
				 int *hot_x, int *hot_y, int frame)
{
  *hot_x = t->sprites.cursor[cursor].hot_x;
  *hot_y = t->sprites.cursor[cursor].hot_y;
  return t->sprites.cursor[cursor].frame[frame];
}

/****************************************************************************
  Return a sprite for the given icon.  Icons are used by the operating
  system/window manager.  Usually freeciv has to tell the OS what icon to
  use.

  Note that this function will return NULL before the sprites are loaded.
  The GUI code must be sure to call tileset_load_tiles before setting the
  top-level icon.
****************************************************************************/
struct sprite *get_icon_sprite(const struct tileset *t, enum icon_type icon)
{
  return t->sprites.icon[icon];
}

/****************************************************************************
  Returns a sprite with the "user-attention" crosshair graphic.

  FIXME: This function shouldn't be needed if the attention graphics are
  drawn natively by the tileset code.
****************************************************************************/
struct sprite *get_attention_crosshair_sprite(const struct tileset *t)
{
  return t->sprites.user.attention;
}

/****************************************************************************
  Returns a sprite for the given indicator with the given index.  The
  index should be in [0, NUM_TILES_PROGRESS).
****************************************************************************/
struct sprite *get_indicator_sprite(const struct tileset *t,
                                    enum indicator_type indicator,
                                    int index)
{
  index = CLIP(0, index, NUM_TILES_PROGRESS - 1);
  fc_assert_ret_val(indicator >= 0 && indicator < INDICATOR_COUNT, NULL);
  return t->sprites.indicator[indicator][index];
}

/****************************************************************************
  Return a sprite for the unhappiness of the unit - to be shown as an
  overlay on the unit in the city support dialog, for instance.

  May return NULL if there's no unhappiness.
****************************************************************************/
struct sprite *get_unit_unhappy_sprite(const struct tileset *t,
				       const struct unit *punit,
				       int happy_cost)
{
  const int unhappy = CLIP(0, happy_cost, 2);

  if (unhappy > 0) {
    return t->sprites.upkeep.unhappy[unhappy - 1];
  } else {
    return NULL;
  }
}

/****************************************************************************
  Return a sprite for the upkeep of the unit - to be shown as an overlay
  on the unit in the city support dialog, for instance.

  May return NULL if there's no unhappiness.
****************************************************************************/
struct sprite *get_unit_upkeep_sprite(const struct tileset *t,
				      Output_type_id otype,
				      const struct unit *punit,
				      const int *upkeep_cost)
{
  const int upkeep = CLIP(0, upkeep_cost[otype], 2);

  if (upkeep > 0) {
    return t->sprites.upkeep.output[otype][upkeep - 1];
  } else {
    return NULL;
  }
}

/****************************************************************************
  Return a rectangular sprite containing a fog "color".  This can be used
  for drawing fog onto arbitrary areas (like the overview).
****************************************************************************/
struct sprite *get_basic_fog_sprite(const struct tileset *t)
{
  return t->sprites.tx.fog;
}

/****************************************************************************
  Return the tileset's color system.
****************************************************************************/
struct color_system *get_color_system(const struct tileset *t)
{
  return t->color_system;
}

/****************************************************************************
  Loads prefered theme if there's any.
****************************************************************************/
void tileset_use_prefered_theme(const struct tileset *t)
{
  char *default_theme_name = NULL;
  size_t default_theme_name_sz = 0;
  int i;

  switch (get_gui_type()) {
  case GUI_GTK2:
    default_theme_name = gui_gtk2_default_theme_name;
    default_theme_name_sz = sizeof(gui_gtk2_default_theme_name);
    break;
  case GUI_GTK3:
    default_theme_name = gui_gtk3_default_theme_name;
    default_theme_name_sz = sizeof(gui_gtk3_default_theme_name);
    break;
  case GUI_SDL:
    default_theme_name = gui_sdl_default_theme_name;
    default_theme_name_sz = sizeof(gui_sdl_default_theme_name);
    break;
  case GUI_STUB:
  case GUI_XAW:
  case GUI_QT:
  case GUI_WIN32:
  case GUI_WEB:
    break;
  }

  if (NULL == default_theme_name || 0 == default_theme_name_sz) {
    /* Theme is not supported by this client. */
    return;
  }

  for (i = 0; i < t->num_prefered_themes; i++) {
    if (strcmp(t->prefered_themes[i], default_theme_name)) {
      if (popup_theme_suggestion_dialog(t->prefered_themes[i])) {
        log_debug("trying theme \"%s\".", t->prefered_themes[i]);
        if (load_theme(t->prefered_themes[i])) {
          (void) fc_strlcpy(default_theme_name, t->prefered_themes[i],
                            default_theme_name_sz);
          return;
        }
      }
    }
  }
  log_verbose("The tileset doesn't specify preferred themes or none of its "
              "preferred themes can be used. Using system default.");
  gui_clear_theme();
}

/****************************************************************************
  Initialize tileset structure
****************************************************************************/
void tileset_init(struct tileset *t)
{
  /* We currently have no city sprites loaded. */
  t->sprites.city.tile     = NULL;
  t->sprites.city.wall     = NULL;
  t->sprites.city.occupied = NULL;

  t->sprites.background.color = NULL;
  t->sprites.background.graphic = NULL;

  player_slots_iterate(pslot) {
    int i, j, id = player_slot_index(pslot);

    for (i = 0; i < EDGE_COUNT; i++) {
      for (j = 0; j < 2; j++) {
        t->sprites.player[id].grid_borders[i][j] = NULL;
      }
    }

    t->sprites.player[id].color = NULL;
    t->sprites.player[id].background = NULL;
  } player_slots_iterate_end;
}

/****************************************************************************
  Fill the sprite array with sprites that together make a representative
  image of the given terrain type. Suitable for use as an icon and in list
  views.

  NB: The 'layer' argument is NOT a LAYER_* value, but rather one of 0, 1, 2.
  Using other values for 'layer' here will result in undefined behaviour. ;)
****************************************************************************/
int fill_basic_terrain_layer_sprite_array(struct tileset *t,
                                          struct drawn_sprite *sprs,
                                          int layer,
                                          struct terrain *pterrain)
{
  struct drawn_sprite *save_sprs = sprs;
  struct drawing_data *draw = t->sprites.drawing[terrain_index(pterrain)];

  struct terrain *tterrain_near[8];
  bv_special tspecial_near[8];

  struct tile dummy_tile; /* :( */

  int i;


  memset(&dummy_tile, 0, sizeof(struct tile));
  
  for (i = 0; i < 8; i++) {
    tterrain_near[i] = pterrain;
    BV_CLR_ALL(tspecial_near[i]);
  }

  i = draw->is_reversed ? draw->num_layers - layer - 1 : layer;
  sprs += fill_terrain_sprite_array(t, sprs, i, &dummy_tile,
                                    pterrain, tterrain_near, draw);

  return sprs - save_sprs;
}

/****************************************************************************
  Return the sprite for the given resource type.
****************************************************************************/
struct sprite *get_resource_sprite(const struct tileset *t,
                                   const struct resource *presource)
{
  if (presource == NULL) {
    return NULL;
  }

  return t->sprites.resource[resource_index(presource)];
}

/****************************************************************************
  Return a representative sprite for the mine special type (S_MINE).
****************************************************************************/
struct sprite *get_basic_mine_sprite(const struct tileset *t)
{
  struct drawing_data *draw;
  struct terrain *tm;

  tm = terrain_by_rule_name("mountains");
  if (tm != NULL) {
    draw = t->sprites.drawing[terrain_index(tm)];
    if (draw->mine != NULL) {
      return draw->mine;
    }
  }

  terrain_type_iterate(pterrain) {
    draw = t->sprites.drawing[terrain_index(pterrain)];
    if (draw->mine != NULL) {
      return draw->mine;
    }
  } terrain_type_iterate_end;

  return NULL;
}

/****************************************************************************
  Return a representative sprite for the given special type.

  NB: This does not include generic base specials (S_FORTRESS and
  S_AIRBASE). Use fill_basic_base_sprite_array and the base type for that.
****************************************************************************/
struct sprite *get_basic_special_sprite(const struct tileset *t,
                                        enum tile_special_type special)
{
  switch (special) {
  case S_IRRIGATION:
    return t->sprites.tx.irrigation[0];
    break;
  case S_MINE:
    return get_basic_mine_sprite(t);
    break;
  case S_POLLUTION:
    return t->sprites.tx.pollution;
    break;
  case S_HUT:
    return t->sprites.tx.village;
    break;
  case S_RIVER:
    return t->sprites.tx.rivers.spec[0];
    break;
  case S_FARMLAND:
    return t->sprites.tx.farmland[0];
    break;
  case S_FALLOUT:
    return t->sprites.tx.fallout;
    break;
  default:
    break;
  }

  return NULL;
}

/****************************************************************************
  Fills the sprite array with sprites that together make a representative
  image of the given road type. The image is suitable for use as an icon
  for the road type, for example.
****************************************************************************/
int fill_basic_road_sprite_array(const struct tileset *t,
                                 struct drawn_sprite *sprs,
                                 const struct road_type *proad)
{
  struct drawn_sprite *saved_sprs = sprs;
  int index;
  int i;
  enum roadstyle_id roadstyle;

  if (!t || !sprs || !proad) {
    return 0;
  }

  index = road_index(proad);

  if (!(0 <= index && index < game.control.num_road_types)) {
    return 0;
  }

  roadstyle = t->sprites.roads[index].roadstyle;

  if (roadstyle == RSTYLE_RIVER) {
    return 0;
  }

  for (i = 0; i < t->num_valid_tileset_dirs; i++) {
    if (!t->valid_tileset_dirs[i]) {
      continue;
    }
    if (roadstyle == RSTYLE_ALL_SEPARATE) {
      ADD_SPRITE_FULL(t->sprites.roads[index].u.dir[i]);
    } else if (roadstyle == RSTYLE_PARITY_COMBINED) {
      if ((i % 2) == 0) {
        ADD_SPRITE_FULL(t->sprites.roads[index].u.combo.even[1 << (i / 2)]);
      }
    } else if (roadstyle == RSTYLE_ALL_COMBINED) {
      ADD_SPRITE_FULL(t->sprites.roads[index].u.total[1 << i]);
    }
  }

  return sprs - saved_sprs;
}

/****************************************************************************
  Fills the sprite array with sprites that together make a representative
  image of the given base type. The image is suitable for use as an icon
  for the base type, for example.
****************************************************************************/
int fill_basic_base_sprite_array(const struct tileset *t,
                                 struct drawn_sprite *sprs,
                                 const struct base_type *pbase)
{
  struct drawn_sprite *saved_sprs = sprs;
  int index;

  if (!t || !sprs || !pbase) {
    return 0;
  }

  index = base_index(pbase);

  if (!(0 <= index && index < game.control.num_base_types)) {
    return 0;
  }

#define ADD_SPRITE_IF_NOT_NULL(x) do {\
  if ((x) != NULL) {\
    ADD_SPRITE_FULL(x);\
  }\
} while (0)

  /* Corresponds to LAYER_SPECIAL{1,2,3} order. */
  ADD_SPRITE_IF_NOT_NULL(t->sprites.bases[index].background);
  ADD_SPRITE_IF_NOT_NULL(t->sprites.bases[index].middleground);
  ADD_SPRITE_IF_NOT_NULL(t->sprites.bases[index].foreground);

#undef ADD_SPRITE_IF_NOT_NULL

  return sprs - saved_sprs;
}

/****************************************************************************
  Setup tiles for one player using the player color.
****************************************************************************/
void tileset_player_init(struct tileset *t, struct player *pplayer)
{
  int plrid, i, j;

  fc_assert_ret(pplayer != NULL);
  fc_assert_ret(pplayer->rgb != NULL);

  /* Free all data before recreating it. */
  tileset_player_free(t, pplayer);

  plrid = player_index(pplayer);

  t->sprites.player[plrid].color
    = create_plr_sprite(ensure_color(pplayer->rgb));
  t->sprites.player[plrid].background
    = crop_sprite(t->sprites.player[plrid].color, 0, 0,
                  t->normal_tile_width, t->normal_tile_height,
                  t->sprites.mask.tile, 0, 0);

  for (i = 0; i < EDGE_COUNT; i++) {
    for (j = 0; j < 2; j++) {
      struct sprite *s;

      if (t->sprites.player[plrid].color
          && t->sprites.grid.borders[i][j]) {
        s = crop_sprite(t->sprites.player[plrid].color, 0, 0,
                        t->normal_tile_width, t->normal_tile_height,
                        t->sprites.grid.borders[i][j], 0, 0);
      } else {
        s = t->sprites.grid.borders[i][j];
      }
      t->sprites.player[plrid].grid_borders[i][j] = s;
    }
  }
}

/****************************************************************************
  Free tiles for one player using the player color.
****************************************************************************/
void tileset_player_free(struct tileset *t, struct player *pplayer)
{
  int plrid, i, j;

  fc_assert_ret(pplayer != NULL);

  plrid = player_index(pplayer);

  if (t->sprites.player[plrid].color) {
    free_sprite(t->sprites.player[plrid].color);
    t->sprites.player[plrid].color = NULL;
  }
  if (t->sprites.player[plrid].background) {
    free_sprite(t->sprites.player[plrid].background);
    t->sprites.player[plrid].background = NULL;
  }

  for (i = 0; i < EDGE_COUNT; i++) {
    for (j = 0; j < 2; j++) {
      if (t->sprites.player[plrid].grid_borders[i][j]) {
        free_sprite(t->sprites.player[plrid].grid_borders[i][j]);
        t->sprites.player[plrid].grid_borders[i][j] = NULL;
      }
    }
  }
}

/****************************************************************************
  Setup tiles for the background.
****************************************************************************/
void tileset_background_init(struct tileset *t)
{
  /* Free all data before recreating it. */
  tileset_background_free(t);

  /* generate background color */
  t->sprites.background.color
    = create_plr_sprite(ensure_color(game.plr_bg_color));

  /* Chop up and build the background graphics. */
  t->sprites.background.graphic
    = crop_sprite(t->sprites.background.color, 0, 0,
                  t->normal_tile_width, t->normal_tile_height,
                  t->sprites.mask.tile, 0, 0);
}

/****************************************************************************
  Free tiles for the background.
****************************************************************************/
void tileset_background_free(struct tileset *t)
{
  if (t->sprites.background.color) {
    free_sprite(t->sprites.background.color);
    t->sprites.background.color = NULL;
  }

  if (t->sprites.background.graphic) {
    free_sprite(t->sprites.background.graphic);
    t->sprites.background.graphic = NULL;
  }
}
