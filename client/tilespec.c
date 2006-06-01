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
#include <config.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>		/* exit */
#include <string.h>

#include "astring.h"
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

#include "dialogs_g.h"
#include "graphics_g.h"
#include "mapview_g.h"		/* for update_map_canvas_visible */

#include "civclient.h"		/* for get_client_state() */
#include "climap.h"		/* for tile_get_known() */
#include "control.h"		/* for fill_xxx */
#include "options.h"		/* for fill_xxx */

#include "tilespec.h"

#define TILESPEC_SUFFIX ".tilespec"

char *main_intro_filename;
char *minimap_intro_filename;

struct named_sprites sprites;

/* Stores the currently loaded tileset.  This differs from the value in
 * options.h since that variable is changed by the GUI code. */
static char current_tileset[512];

static const int DIR4_TO_DIR8[4] =
    { DIR8_NORTH, DIR8_SOUTH, DIR8_EAST, DIR8_WEST };

int NORMAL_TILE_WIDTH;
int NORMAL_TILE_HEIGHT;
int UNIT_TILE_WIDTH;
int UNIT_TILE_HEIGHT;
int SMALL_TILE_WIDTH;
int SMALL_TILE_HEIGHT;

int OVERVIEW_TILE_SIZE = 2;

bool is_isometric;
int hex_width, hex_height;

char *city_names_font;
char *city_productions_font_name;

int num_tiles_explode_unit=0;

static int roadstyle;
int fogstyle;
static int flag_offset_x, flag_offset_y;

#define NUM_CORNER_DIRS 4
#define TILES_PER_CORNER 4

static int num_valid_tileset_dirs, num_cardinal_tileset_dirs;
static int num_index_valid, num_index_cardinal;
static enum direction8 valid_tileset_dirs[8], cardinal_tileset_dirs[8];

static struct {
  enum match_style match_style;
  int count;
  char **match_types;
} layers[MAX_NUM_LAYERS];

/* Darkness style.  Don't reorder this enum since tilesets depend on it. */
static enum {
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
  DARKNESS_CARD_FULL = 3
} darkness_style;

struct specfile;

#define SPECLIST_TAG specfile
#define SPECLIST_TYPE struct specfile
#include "speclist.h"

#define specfile_list_iterate(list, pitem) \
    TYPED_LIST_ITERATE(struct specfile, list, pitem)
#define specfile_list_iterate_end  LIST_ITERATE_END

struct small_sprite;
#define SPECLIST_TAG small_sprite
#define SPECLIST_TYPE struct small_sprite
#include "speclist.h"

#define small_sprite_list_iterate(list, pitem) \
    TYPED_LIST_ITERATE(struct small_sprite, list, pitem)
#define small_sprite_list_iterate_end  LIST_ITERATE_END

static struct specfile_list specfiles;
static struct small_sprite_list small_sprites;

struct specfile {
  struct Sprite *big_sprite;
  char *file_name;
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

  struct Sprite *sprite;
};

/*
 * This hash table maps tilespec tags to struct small_sprites.
 */
static struct hash_table *sprite_hash = NULL;

/* This hash table maps terrain graphic strings to drawing data. */
static struct hash_table *terrain_hash;

#define TILESPEC_CAPSTR "+tilespec3 duplicates_ok"
/*
 * Tilespec capabilities acceptable to this program:
 *
 * +tilespec3     -  basic format; required
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


/*
  If focus_unit_hidden is true, then no units at
  the location of the foc unit are ever drawn.
*/
static bool focus_unit_hidden = FALSE;

static struct Sprite* lookup_sprite_tag_alt(const char *tag, const char *alt,
					    bool required, const char *what,
					    const char *name);

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
  assert(0);
  return "";
}

/****************************************************************************
  Return TRUE iff the dir is valid in this tileset.
****************************************************************************/
static bool is_valid_tileset_dir(enum direction8 dir)
{
  if (hex_width > 0) {
    return dir != DIR8_NORTHEAST && dir != DIR8_SOUTHWEST;
  } else if (hex_height > 0) {
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
static bool is_cardinal_tileset_dir(enum direction8 dir)
{
  if (hex_width > 0 || hex_height > 0) {
    return is_valid_tileset_dir(dir);
  } else {
    return (dir == DIR8_NORTH || dir == DIR8_EAST
	    || dir == DIR8_SOUTH || dir == DIR8_WEST);
  }
}

/**********************************************************************
  Returns a static list of tilesets available on the system by
  searching all data directories for files matching TILESPEC_SUFFIX.
  The list is NULL-terminated.
***********************************************************************/
const char **get_tileset_list(void)
{
  static const char **tileset_list = NULL;

  if (!tileset_list) {
    /* Note: this means you must restart the client after installing a new
       tileset. */
    tileset_list = datafilelist(TILESPEC_SUFFIX);
  }

  return tileset_list;
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
  const char *tileset_default;
  char *fname, *dname;

  if (isometric_view_supported()) {
    tileset_default = "isotrident"; /* Do not i18n! --dwp */
  } else {
    tileset_default = "trident";    /* Do not i18n! --dwp */
  }

  if (!tileset_name || tileset_name[0] == '\0') {
    tileset_name = tileset_default;
  }

  /* Hack: this is the name of the tileset we're about to load.  We copy
   * it here, since this is the only place where we know it.  Note this
   * also means if you do "civ -t foo" this will change your *default*
   * tileset to 'foo'. */
  sz_strlcpy(default_tileset_name, tileset_name);

  fname = fc_malloc(strlen(tileset_name) + strlen(TILESPEC_SUFFIX) + 1);
  sprintf(fname, "%s%s", tileset_name, TILESPEC_SUFFIX);
  
  dname = datafilename(fname);
  free(fname);

  if (dname) {
    return mystrdup(dname);
  }

  if (strcmp(tileset_name, tileset_default) == 0) {
    freelog(LOG_FATAL, _("No usable default tileset found, aborting!"));
    exit(EXIT_FAILURE);
  }
  freelog(LOG_ERROR, _("Trying \"%s\" tileset."), tileset_default);
  return tilespec_fullname(tileset_default);
}

/**********************************************************************
  Checks options in filename match what we require and support.
  Die if not.
  'which' should be "tilespec" or "spec".
***********************************************************************/
static bool check_tilespec_capabilities(struct section_file *file,
					const char *which,
					const char *us_capstr,
					const char *filename)
{
  char *file_capstr = secfile_lookup_str(file, "%s.options", which);
  
  if (!has_capabilities(us_capstr, file_capstr)) {
    freelog(LOG_ERROR, _("%s file appears incompatible:\n"
			 "file: \"%s\"\n"
			 "file options: %s\n"
			 "supported options: %s"),
	    which, filename, file_capstr, us_capstr);
    return FALSE;
  }
  if (!has_capabilities(file_capstr, us_capstr)) {
    freelog(LOG_ERROR, _("%s file claims required option(s)"
			 " which we don't support:\n"
			 "file: \"%s\"\n"
			 "file options: %s\n"
			 "supported options: %s"),
	    which, filename, file_capstr, us_capstr);
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************
  Frees the tilespec toplevel data, in preparation for re-reading it.

  See tilespec_read_toplevel().
***********************************************************************/
static void tilespec_free_toplevel(void)
{
  if (city_names_font) {
    free(city_names_font);
    city_names_font = NULL;
  }
  if (city_productions_font_name) {
    free(city_productions_font_name);
    city_productions_font_name = NULL;
  }
  if (main_intro_filename) {
    free(main_intro_filename);
    main_intro_filename = NULL;
  }
  if (minimap_intro_filename) {
    free(minimap_intro_filename);
    minimap_intro_filename = NULL;
  }

  while (hash_num_entries(terrain_hash) > 0) {
    const struct terrain_drawing_data *draw;

    draw = hash_value_by_number(terrain_hash, 0);
    hash_delete_entry(terrain_hash, draw->name);
    free(draw->name);
    if (draw->mine_tag) {
      free(draw->mine_tag);
    }
    free((void *) draw);
  }
  hash_free(terrain_hash);
  terrain_hash = NULL; /* Helpful for sanity. */
}

/**********************************************************************
  Read a new tilespec in from scratch.

  Unlike the initial reading code, which reads pieces one at a time,
  this gets rid of the old data and reads in the new all at once.  If the
  new tileset fails to load the old tileset may be reloaded; otherwise the
  client will exit.

  It will also call the necessary functions to redraw the graphics.
***********************************************************************/
void tilespec_reread(const char *tileset_name)
{
  int id;
  struct tile *center_tile;
  enum client_states state = get_client_state();

  freelog(LOG_NORMAL, "Loading tileset %s.", tileset_name);

  /* Step 0:  Record old data.
   *
   * We record the current mapcanvas center, etc.
   */
  center_tile = get_center_tile_mapcanvas();

  /* Step 1:  Cleanup.
   *
   * We free all old data in preparation for re-reading it.
   */
  tilespec_free_tiles();
  tilespec_free_city_tiles(game.styles_count);
  tilespec_free_toplevel();

  /* Step 2:  Read.
   *
   * We read in the new tileset.  This should be pretty straightforward.
   */
  if (!tilespec_read_toplevel(tileset_name)) {
    if (!tilespec_read_toplevel(current_tileset)) {
      die("Failed to re-read the currently loaded tileset.");
    }
  }
  tilespec_load_tiles();

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
  if (state < CLIENT_SELECT_RACE_STATE) {
    /* The ruleset data is not sent until this point. */
    return;
  }
  for (id = T_FIRST; id < T_COUNT; id++) {
    tilespec_setup_tile_type(id);
  }
  unit_type_iterate(id) {
    tilespec_setup_unit_type(id);
  } unit_type_iterate_end;
  government_iterate(gov) {
    tilespec_setup_government(gov->index);
  } government_iterate_end;
  for (id = 0; id < game.nation_count; id++) {
    tilespec_setup_nation_flag(id);
  }
  impr_type_iterate(imp_id) {
    tilespec_setup_impr_type(imp_id);
  } impr_type_iterate_end;
  tech_type_iterate(tech_id) {
    if (tech_id != A_NONE && tech_exists(tech_id)) {
      tilespec_setup_tech_type(tech_id);
    }
  } tech_type_iterate_end;
  tilespec_setup_specialist_types();

  /* tilespec_load_tiles reverts the city tile pointers to 0.  This
     is a workaround. */
  tilespec_alloc_city_tiles(game.styles_count);
  for (id = 0; id < game.styles_count; id++) {
    tilespec_setup_city_tiles(id);
  }

  /* Step 4:  Draw.
   *
   * Do any necessary redraws.
   */
  if (state < CLIENT_GAME_RUNNING_STATE) {
    /* Unless the client state is playing a game or in gameover,
       we don't want/need to redraw. */
    return;
  }
  popdown_all_game_dialogs();
  generate_citydlg_dimensions();
  tileset_changed();
  can_slide = FALSE;
  center_tile_mapcanvas(center_tile);
  /* update_map_cavnas_visible forces a full redraw.  Otherwise with fast
   * drawing we might not get one.  Of course this is slower. */
  update_map_canvas_visible();
  can_slide = TRUE;
}

/**************************************************************************
  This is merely a wrapper for tilespec_reread (above) for use in
  options.c and the client local options dialog.
**************************************************************************/
void tilespec_reread_callback(struct client_option *option)
{
  assert(option->p_string_value && *option->p_string_value != '\0');
  tilespec_reread(option->p_string_value);
}

/**************************************************************************
  Loads the given graphics file (found in the data path) into a newly
  allocated sprite.
**************************************************************************/
static struct Sprite *load_gfx_file(const char *gfx_filename)
{
  const char **gfx_fileexts = gfx_fileextensions(), *gfx_fileext;
  struct Sprite *s;

  /* Try out all supported file extensions to find one that works. */
  while ((gfx_fileext = *gfx_fileexts++)) {
    char *real_full_name;
    char full_name[strlen(gfx_filename) + strlen(gfx_fileext) + 2];

    sprintf(full_name, "%s.%s", gfx_filename, gfx_fileext);
    if ((real_full_name = datafilename(full_name))) {
      freelog(LOG_DEBUG, "trying to load gfx file %s", real_full_name);
      s = load_gfxfile(real_full_name);
      if (s) {
	return s;
      }
    }
  }

  freelog(LOG_VERBOSE, "Could not load gfx file %s.", gfx_filename);
  return NULL;
}

/**************************************************************************
  Ensure that the big sprite of the given spec file is loaded.
**************************************************************************/
static void ensure_big_sprite(struct specfile *sf)
{
  struct section_file the_file, *file = &the_file;
  const char *gfx_filename;

  if (sf->big_sprite) {
    /* Looks like it's already loaded. */
    return;
  }

  /* Otherwise load it.  The big sprite will sometimes be freed and will have
   * to be reloaded, but most of the time it's just loaded once, the small
   * sprites are extracted, and then it's freed. */
  if (!section_file_load(file, sf->file_name)) {
    freelog(LOG_FATAL, _("Could not open \"%s\"."), sf->file_name);
    exit(EXIT_FAILURE);
  }

  if (!check_tilespec_capabilities(file, "spec",
				   SPEC_CAPSTR, sf->file_name)) {
    exit(EXIT_FAILURE);
  }

  gfx_filename = secfile_lookup_str(file, "file.gfx");

  sf->big_sprite = load_gfx_file(gfx_filename);

  if (!sf->big_sprite) {
    freelog(LOG_FATAL, _("Couldn't load gfx file for the spec file %s"),
	    sf->file_name);
    exit(EXIT_FAILURE);
  }
  section_file_free(file);
}

/**************************************************************************
  Scan all sprites declared in the given specfile.  This means that the
  positions of the sprites in the big_sprite are saved in the
  small_sprite structs.
**************************************************************************/
static void scan_specfile(struct specfile *sf, bool duplicates_ok)
{
  struct section_file the_file, *file = &the_file;
  char **gridnames;
  int num_grids, i;

  if (!section_file_load(file, sf->file_name)) {
    freelog(LOG_FATAL, _("Could not open \"%s\"."), sf->file_name);
    exit(EXIT_FAILURE);
  }
  if (!check_tilespec_capabilities(file, "spec",
				   SPEC_CAPSTR, sf->file_name)) {
    exit(EXIT_FAILURE);
  }

  /* currently unused */
  (void) section_file_lookup(file, "info.artists");

  gridnames = secfile_get_secnames_prefix(file, "grid_", &num_grids);

  for (i = 0; i < num_grids; i++) {
    int j, k;
    int x_top_left, y_top_left, dx, dy;
    int pixel_border;

    pixel_border =
      secfile_lookup_int_default(file, -1, "%s.pixel_border", gridnames[i]);
    if (pixel_border < 0) {
      /* is_pixel_border is used in old tilesets. */
      pixel_border =
	(secfile_lookup_bool_default(file, FALSE,
				     "%s.is_pixel_border", gridnames[i])
	 ? 1 : 0);
    }

    x_top_left = secfile_lookup_int(file, "%s.x_top_left", gridnames[i]);
    y_top_left = secfile_lookup_int(file, "%s.y_top_left", gridnames[i]);
    dx = secfile_lookup_int(file, "%s.dx", gridnames[i]);
    dy = secfile_lookup_int(file, "%s.dy", gridnames[i]);

    j = -1;
    while (section_file_lookup(file, "%s.tiles%d.tag", gridnames[i], ++j)) {
      struct small_sprite *ss = fc_malloc(sizeof(*ss));
      int row, column;
      int x1, y1;
      char **tags;
      int num_tags;

      row = secfile_lookup_int(file, "%s.tiles%d.row", gridnames[i], j);
      column = secfile_lookup_int(file, "%s.tiles%d.column", gridnames[i], j);
      tags = secfile_lookup_str_vec(file, &num_tags, "%s.tiles%d.tag",
				    gridnames[i], j);

      /* there must be at least 1 because of the while(): */
      assert(num_tags > 0);

      x1 = x_top_left + (dx + pixel_border) * column;
      y1 = y_top_left + (dy + pixel_border) * row;

      ss->ref_count = 0;
      ss->file = NULL;
      ss->x = x1;
      ss->y = y1;
      ss->width = dx;
      ss->height = dy;
      ss->sf = sf;
      ss->sprite = NULL;

      small_sprite_list_insert(&small_sprites, ss);

      if (!duplicates_ok) {
        for (k = 0; k < num_tags; k++) {
          if (!hash_insert(sprite_hash, mystrdup(tags[k]), ss)) {
	    freelog(LOG_ERROR, "warning: already have a sprite for %s", tags[k]);
          }
        }
      } else {
        for (k = 0; k < num_tags; k++) {
      (void) hash_replace(sprite_hash, mystrdup(tags[k]), ss);
        }
      }

      free(tags);
      tags = NULL;
    }
  }
  free(gridnames);
  gridnames = NULL;

  /* Load "extra" sprites.  Each sprite is one file. */
  i = -1;
  while (secfile_lookup_str_default(file, NULL, "extra.sprites%d.tag", ++i)) {
    struct small_sprite *ss = fc_malloc(sizeof(*ss));
    char **tags;
    char *filename;
    int num_tags, k;

    tags
      = secfile_lookup_str_vec(file, &num_tags, "extra.sprites%d.tag", i);
    filename = secfile_lookup_str(file, "extra.sprites%d.file", i);

    ss->ref_count = 0;
    ss->file = mystrdup(filename);
    ss->sf = NULL;
    ss->sprite = NULL;

    small_sprite_list_insert(&small_sprites, ss);

    if (!duplicates_ok) {
      for (k = 0; k < num_tags; k++) {
	if (!hash_insert(sprite_hash, mystrdup(tags[k]), ss)) {
	  freelog(LOG_ERROR, "warning: already have a sprite for %s", tags[k]);
	}
      }
    } else {
      for (k = 0; k < num_tags; k++) {
	(void) hash_replace(sprite_hash, mystrdup(tags[k]), ss);
      }
    }
    free(tags);
  }

  section_file_check_unused(file, sf->file_name);
  section_file_free(file);
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
    char *real_full_name;

    sprintf(full_name,"%s.%s",gfx_filename,gfx_current_fileext);

    real_full_name = datafilename(full_name);
    free(full_name);
    if (real_full_name) {
      return mystrdup(real_full_name);
    }
  }

  freelog(LOG_FATAL, _("Couldn't find a supported gfx file extension for %s"),
         gfx_filename);
  exit(EXIT_FAILURE);
  return NULL;
}

/**********************************************************************
  Finds and reads the toplevel tilespec file based on given name.
  Sets global variables, including tile sizes and full names for
  intro files.
***********************************************************************/
bool tilespec_read_toplevel(const char *tileset_name)
{
  struct section_file the_file, *file = &the_file;
  char *fname, *c;
  int i;
  int num_spec_files, num_terrains, hex_side;
  char **spec_filenames, **terrains;
  char *file_capstr;
  bool duplicates_ok, is_hex;
  enum direction8 dir;

  fname = tilespec_fullname(tileset_name);
  freelog(LOG_VERBOSE, "tilespec file is %s", fname);

  if (!section_file_load(file, fname)) {
    free(fname);
    freelog(LOG_ERROR, _("Could not open \"%s\"."), fname);
    return FALSE;
  }

  if (!check_tilespec_capabilities(file, "tilespec",
				   TILESPEC_CAPSTR, fname)) {
    section_file_free(file);
    free(fname);
    return FALSE;
  }

  file_capstr = secfile_lookup_str(file, "%s.options", "tilespec");
  duplicates_ok = has_capabilities("+duplicates_ok", file_capstr);

  (void) section_file_lookup(file, "tilespec.name"); /* currently unused */

  is_isometric = secfile_lookup_bool_default(file, FALSE, "tilespec.is_isometric");

  /* Read hex-tileset information. */
  is_hex = secfile_lookup_bool_default(file, FALSE, "tilespec.is_hex");
  hex_side = secfile_lookup_int_default(file, 0, "tilespec.hex_side");
  hex_width = hex_height = 0;
  if (is_hex) {
    if (is_isometric) {
      hex_height = hex_side;
    } else {
      hex_width = hex_side;
    }
    is_isometric = TRUE; /* Hex tilesets are drawn the same as isometric. */
  }

  if (is_isometric && !isometric_view_supported()) {
    freelog(LOG_ERROR, _("Client does not support isometric tilesets."
	    " Using default tileset instead."));
    assert(tileset_name != NULL);
    section_file_free(file);
    free(fname);
    return tilespec_read_toplevel(NULL);
  }
  if (!is_isometric && !overhead_view_supported()) {
    freelog(LOG_ERROR, _("Client does not support overhead view tilesets."
	    " Using default tileset instead."));
    assert(tileset_name != NULL);
    section_file_free(file);
    free(fname);
    return tilespec_read_toplevel(NULL);
  }

  /* Create arrays of valid and cardinal tileset dirs.  These depend
   * entirely on the tileset, not the topology.  They are also in clockwise
   * rotational ordering. */
  num_valid_tileset_dirs = num_cardinal_tileset_dirs = 0;
  dir = DIR8_NORTH;
  do {
    if (is_valid_tileset_dir(dir)) {
      valid_tileset_dirs[num_valid_tileset_dirs] = dir;
      num_valid_tileset_dirs++;
    }
    if (is_cardinal_tileset_dir(dir)) {
      cardinal_tileset_dirs[num_cardinal_tileset_dirs] = dir;
      num_cardinal_tileset_dirs++;
    }

    dir = dir_cw(dir);
  } while (dir != DIR8_NORTH);
  assert(num_valid_tileset_dirs % 2 == 0); /* Assumed elsewhere. */
  num_index_valid = 1 << num_valid_tileset_dirs;
  num_index_cardinal = 1 << num_cardinal_tileset_dirs;

  NORMAL_TILE_WIDTH = secfile_lookup_int(file, "tilespec.normal_tile_width");
  NORMAL_TILE_HEIGHT = secfile_lookup_int(file, "tilespec.normal_tile_height");
  if (is_isometric) {
    UNIT_TILE_WIDTH = NORMAL_TILE_WIDTH;
    UNIT_TILE_HEIGHT = 3 * NORMAL_TILE_HEIGHT/2;
  } else {
    UNIT_TILE_WIDTH = NORMAL_TILE_WIDTH;
    UNIT_TILE_HEIGHT = NORMAL_TILE_HEIGHT;
  }
  SMALL_TILE_WIDTH = secfile_lookup_int(file, "tilespec.small_tile_width");
  SMALL_TILE_HEIGHT = secfile_lookup_int(file, "tilespec.small_tile_height");
  freelog(LOG_VERBOSE, "tile sizes %dx%d, %d%d unit, %d%d small",
	  NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT,
	  UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT,
	  SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT);

  roadstyle = secfile_lookup_int_default(file, is_isometric ? 0 : 1,
					 "tilespec.roadstyle");
  fogstyle = secfile_lookup_int_default(file, 0,
					"tilespec.fogstyle");
  darkness_style = secfile_lookup_int(file, "tilespec.darkness_style");
  if (darkness_style < DARKNESS_NONE
      || darkness_style > DARKNESS_CARD_FULL
      || (darkness_style == DARKNESS_ISORECT
	  && (!is_isometric || hex_width > 0 || hex_height > 0))) {
    freelog(LOG_FATAL, _("Invalid darkness style set in tileset."));
    exit(EXIT_FAILURE);
  }
  flag_offset_x = secfile_lookup_int_default(file, 0,
					     "tilespec.flag_offset_x");
  flag_offset_y = secfile_lookup_int_default(file, 0,
					     "tilespec.flag_offset_y");

  c = secfile_lookup_str_default(file, "10x20", "tilespec.city_names_font");
  city_names_font = mystrdup(c);

  c =
      secfile_lookup_str_default(file, "8x16",
				 "tilespec.city_productions_font");
  city_productions_font_name = mystrdup(c);

  c = secfile_lookup_str(file, "tilespec.main_intro_file");
  main_intro_filename = tilespec_gfx_filename(c);
  freelog(LOG_DEBUG, "intro file %s", main_intro_filename);
  
  c = secfile_lookup_str(file, "tilespec.minimap_intro_file");
  minimap_intro_filename = tilespec_gfx_filename(c);
  freelog(LOG_DEBUG, "radar file %s", minimap_intro_filename);

  /* Terrain layer info. */
  for (i = 0; i < MAX_NUM_LAYERS; i++) {
    char *style = secfile_lookup_str_default(file, "none",
					     "layer%d.match_style", i);
    int j;

    if (mystrcasecmp(style, "full") == 0) {
      layers[i].match_style = MATCH_FULL;
    } else if (mystrcasecmp(style, "bool") == 0) {
      layers[i].match_style = MATCH_BOOLEAN;
    } else {
      layers[i].match_style = MATCH_NONE;
    }

    layers[i].match_types = secfile_lookup_str_vec(file, &layers[i].count,
						   "layer%d.match_types", i);
    for (j = 0; j < layers[i].count; j++) {
      layers[i].match_types[j] = mystrdup(layers[i].match_types[j]);
    }
  }

  /* Terrain drawing info. */
  terrains = secfile_get_secnames_prefix(file, "terrain_", &num_terrains);
  if (num_terrains == 0) {
    freelog(LOG_ERROR, "No terrain types supported by tileset.");
    section_file_free(file);
    free(fname);
    return FALSE;
  }

  assert(terrain_hash == NULL);
  terrain_hash = hash_new(hash_fval_string, hash_fcmp_string);

  for (i = 0; i < num_terrains; i++) {
    struct terrain_drawing_data *terr = fc_malloc(sizeof(*terr));
    char *cell_type;
    int l, j;

    memset(terr, 0, sizeof(*terr));
    terr->name = mystrdup(terrains[i] + strlen("terrain_"));
    terr->is_blended = secfile_lookup_bool(file, "%s.is_blended",
					    terrains[i]);
    terr->num_layers = secfile_lookup_int(file, "%s.num_layers",
					  terrains[i]);
    terr->num_layers = CLIP(1, terr->num_layers, MAX_NUM_LAYERS);

    for (l = 0; l < terr->num_layers; l++) {
      char *match_type, *match_style;

      terr->layer[l].is_tall
	= secfile_lookup_bool_default(file, FALSE, "%s.layer%d_is_tall",
				      terrains[i], l);
      terr->layer[l].offset_x
	= secfile_lookup_int_default(file, 0, "%s.layer%d_offset_x",
				     terrains[i], l);
      terr->layer[l].offset_y
	= secfile_lookup_int_default(file, 0, "%s.layer%d_offset_y",
				     terrains[i], l);
      match_style = secfile_lookup_str_default(file, "none",
					       "%s.layer%d_match_style",
					       terrains[i], l);
      if (mystrcasecmp(match_style, "full") == 0) {
	terr->layer[l].match_style = MATCH_FULL;
      } else if (mystrcasecmp(match_style, "bool") == 0) {
	terr->layer[l].match_style = MATCH_BOOLEAN;
      } else {
	terr->layer[l].match_style = MATCH_NONE;
      }

      match_type = secfile_lookup_str_default(file, NULL,
					      "%s.layer%d_match_type",
					      terrains[i], l);
      if (match_type) {
	/* Set match_count */
	switch (terr->layer[l].match_style) {
	case MATCH_NONE:
	  terr->layer[l].match_count = 0;
	  break;
	case MATCH_FULL:
	  terr->layer[l].match_count = layers[l].count;
	  break;
	case MATCH_BOOLEAN:
	  terr->layer[l].match_count = 2;
	  break;
	}

	/* Determine our match_type. */
	for (j = 0; j < layers[l].count; j++) {
	  if (mystrcasecmp(layers[l].match_types[j], match_type) == 0) {
	    break;
	  }
	}
	terr->layer[l].match_type = j;
	if (j >= layers[l].count) {
	  freelog(LOG_ERROR, "Invalid match type given for %s.", terrains[i]);
	  terr->layer[l].match_type = 0;
	  terr->layer[l].match_style = MATCH_NONE;
	}
      } else {
	terr->layer[l].match_style = MATCH_NONE;
	if (layers[l].match_style != MATCH_NONE) {
	  freelog(LOG_ERROR, "Layer %d has a match_style set; all terrains"
		  " must have a match_type.  %s doesn't.", l, terrains[i]);
	}
      }

      if (terr->layer[l].match_style == MATCH_NONE
	  && layers[l].match_style == MATCH_FULL) {
	freelog(LOG_ERROR, "Layer %d has match_type full set; all terrains"
		" must match this.  %s doesn't.", l, terrains[i]);
      }

      cell_type
	= secfile_lookup_str_default(file, "single", "%s.layer%d_cell_type",
				     terrains[i], l);
      if (mystrcasecmp(cell_type, "single") == 0) {
	terr->layer[l].cell_type = CELL_SINGLE;
      } else if (mystrcasecmp(cell_type, "rect") == 0) {
	terr->layer[l].cell_type = CELL_RECT;
	if (terr->layer[l].is_tall
	    || terr->layer[l].offset_x > 0
	    || terr->layer[l].offset_y > 0) {
	  freelog(LOG_ERROR,
		  _("Error in %s layer %d: you cannot have tall terrain or\n"
		    "a sprite offset with a cell-based drawing method."),
		  terrains[i], l);
	  terr->layer[l].is_tall = FALSE;
	  terr->layer[l].offset_x = terr->layer[l].offset_y = 0;
	}
      } else {
	freelog(LOG_ERROR, "Unknown cell type %s for %s.",
		cell_type, terrains[i]);
	terr->layer[l].cell_type = CELL_SINGLE;
      }
    }

    terr->mine_tag = secfile_lookup_str_default(file, NULL, "%s.mine_sprite",
						terrains[i]);
    if (terr->mine_tag) {
      terr->mine_tag = mystrdup(terr->mine_tag);
    }

    if (!hash_insert(terrain_hash, terr->name, terr)) {
      freelog(LOG_NORMAL, "warning: duplicate terrain entry %s.",
	      terrains[i]);
      section_file_free(file);
      free(fname);
      return FALSE;
    }
  }
  free(terrains);


  spec_filenames = secfile_lookup_str_vec(file, &num_spec_files,
					  "tilespec.files");
  if (num_spec_files == 0) {
    freelog(LOG_ERROR, "No tile files specified in \"%s\"", fname);
    section_file_free(file);
    free(fname);
    return FALSE;
  }

  sprite_hash = hash_new(hash_fval_string, hash_fcmp_string);
  specfile_list_init(&specfiles);
  small_sprite_list_init(&small_sprites);
  for (i = 0; i < num_spec_files; i++) {
    struct specfile *sf = fc_malloc(sizeof(*sf));

    freelog(LOG_DEBUG, "spec file %s", spec_filenames[i]);
    
    sf->big_sprite = NULL;
    sf->file_name = mystrdup(datafilename_required(spec_filenames[i]));
    scan_specfile(sf, duplicates_ok);

    specfile_list_insert(&specfiles, sf);
  }
  free(spec_filenames);

  section_file_check_unused(file, fname);
  
  section_file_free(file);
  freelog(LOG_VERBOSE, "finished reading %s", fname);
  free(fname);

  sz_strlcpy(current_tileset, tileset_name);

  return TRUE;
}

/**********************************************************************
  Returns a text name for the citizen, as used in the tileset.
***********************************************************************/
static const char *get_citizen_name(struct citizen_type citizen)
{
  /* These strings are used in reading the tileset.  Do not
   * translate. */
  switch (citizen.type) {
  case CITIZEN_SPECIALIST:
    return game.rgame.specialists[citizen.spec_type].name;
  case CITIZEN_HAPPY:
    return "happy";
  case CITIZEN_CONTENT:
    return "content";
  case CITIZEN_UNHAPPY:
    return "unhappy";
  case CITIZEN_ANGRY:
    return "angry";
  case CITIZEN_LAST:
    break;
  }
  die("unknown citizen type %d", (int) citizen.type);
  return NULL;
}

/****************************************************************************
  Return a directional string for the cardinal directions.  Normally the
  binary value 1000 will be converted into "n1e0s0w0".  This is in a
  clockwise ordering.
****************************************************************************/
static const char *cardinal_index_str(int idx)
{
  static char c[64];
  int i;

  c[0] = '\0';
  for (i = 0; i < num_cardinal_tileset_dirs; i++) {
    int value = (idx >> i) & 1;

    snprintf(c + strlen(c), sizeof(c) - strlen(c), "%s%d",
	     dir_get_tileset_name(cardinal_tileset_dirs[i]), value);
  }

  return c;
}

/****************************************************************************
  Do the same thing as cardinal_str, except including all valid directions.
  The returned string is a pointer to static memory.
****************************************************************************/
static char *valid_index_str(int index)
{
  static char c[64];
  int i;

  c[0] = '\0';
  for (i = 0; i < num_valid_tileset_dirs; i++) {
    int value = (index >> i) & 1;

    snprintf(c + strlen(c), sizeof(c) - strlen(c), "%s%d",
	     dir_get_tileset_name(valid_tileset_dirs[i]), value);
  }

  return c;
}
     
/* Not very safe, but convenient: */
#define SET_SPRITE(field, tag) do {                       \
       sprites.field = load_sprite(tag);                  \
       if (!sprites.field) {                              \
         die("Sprite tag %s missing.", tag);              \
       }                                                  \
    } while(FALSE)

/* Sets sprites.field to tag or (if tag isn't available) to alt */
#define SET_SPRITE_ALT(field, tag, alt) do {                   \
       sprites.field = load_sprite(tag);                       \
       if (!sprites.field) {                                   \
           sprites.field = load_sprite(alt);                   \
       }                                                       \
       if (!sprites.field) {                                   \
         die("Sprite tag %s and alternate %s are both missing.", tag, alt); \
       }                                                       \
    } while(FALSE)

/* Sets sprites.field to tag, or NULL if not available */
#define SET_SPRITE_OPT(field, tag) \
  sprites.field = load_sprite(tag)

#define SET_SPRITE_ALT_OPT(field, tag, alt) do {               \
      sprites.field = lookup_sprite_tag_alt(tag, alt, FALSE,   \
					    "sprite", #field); \
    } while (FALSE)

/****************************************************************************
  Setup the graphics for specialist types.
****************************************************************************/
void tilespec_setup_specialist_types(void)
{
  /* Load the specialist sprite graphics. */
  specialist_type_iterate(i) {
    struct citizen_type c = {.type = CITIZEN_SPECIALIST, .spec_type = i};
    const char *name = get_citizen_name(c);
    char buffer[512];
    int j;

    for (j = 0; j < NUM_TILES_CITIZEN; j++) {
      my_snprintf(buffer, sizeof(buffer), "specialist.%s_%d", name, j);
      sprites.specialist[i].sprite[j] = load_sprite(buffer);
      if (!sprites.specialist[i].sprite[j]) {
	break;
      }
    }
    sprites.specialist[i].count = j;
    if (j == 0) {
      freelog(LOG_NORMAL, _("No graphics for specialist %s."), name);
      exit(EXIT_FAILURE);
    }
  } specialist_type_iterate_end;
}

/****************************************************************************
  Setup the graphics for (non-specialist) citizen types.
****************************************************************************/
static void tilespec_setup_citizen_types(void)
{
  int i, j;
  char buffer[512];

  /* Load the citizen sprite graphics. */
  for (i = 0; i < NUM_TILES_CITIZEN; i++) {
    struct citizen_type c = {.type = i};
    const char *name = get_citizen_name(c);

    if (i == CITIZEN_SPECIALIST) {
      continue; /* Handled separately. */
    }

    for (j = 0; j < NUM_TILES_CITIZEN; j++) {
      my_snprintf(buffer, sizeof(buffer), "citizen.%s_%d", name, j);
      sprites.citizen[i].sprite[j] = load_sprite(buffer);
      if (!sprites.citizen[i].sprite[j]) {
	break;
      }
    }
    sprites.citizen[i].count = j;
    if (j == 0) {
      freelog(LOG_NORMAL, _("No graphics for citizen %s."), name);
      exit(EXIT_FAILURE);
    }
  }
}

/**********************************************************************
  Initialize 'sprites' structure based on hardwired tags which
  freeciv always requires. 
***********************************************************************/
static void tilespec_lookup_sprite_tags(void)
{
  char buffer[512];
  const char dir_char[] = "nsew";
  int i;
  
  assert(sprite_hash != NULL);

  SET_SPRITE(treaty_thumb[0], "treaty.disagree_thumb_down");
  SET_SPRITE(treaty_thumb[1], "treaty.agree_thumb_up");

  for(i=0; i<NUM_TILES_PROGRESS; i++) {
    my_snprintf(buffer, sizeof(buffer), "s.science_bulb_%d", i);
    SET_SPRITE(bulb[i], buffer);
    my_snprintf(buffer, sizeof(buffer), "s.warming_sun_%d", i);
    SET_SPRITE(warming[i], buffer);
    my_snprintf(buffer, sizeof(buffer), "s.cooling_flake_%d", i);
    SET_SPRITE(cooling[i], buffer);
  }

  SET_SPRITE(right_arrow, "s.right_arrow");
  if (is_isometric) {
    SET_SPRITE(black_tile, "t.black_tile");
    SET_SPRITE(dither_tile, "t.dither_tile");
  }

  SET_SPRITE(tax_luxury, "s.tax_luxury");
  SET_SPRITE(tax_science, "s.tax_science");
  SET_SPRITE(tax_gold, "s.tax_gold");

  tilespec_setup_citizen_types();

  SET_SPRITE(spaceship.solar_panels, "spaceship.solar_panels");
  SET_SPRITE(spaceship.life_support, "spaceship.life_support");
  SET_SPRITE(spaceship.habitation,   "spaceship.habitation");
  SET_SPRITE(spaceship.structural,   "spaceship.structural");
  SET_SPRITE(spaceship.fuel,         "spaceship.fuel");
  SET_SPRITE(spaceship.propulsion,   "spaceship.propulsion");

  /* Isolated road graphics are used by roadstyle 0 and 1*/
  if (roadstyle == 0 || roadstyle == 1) {
    SET_SPRITE(road.isolated, "r.road_isolated");
    SET_SPRITE(rail.isolated, "r.rail_isolated");
  }
  
  if (roadstyle == 0) {
    /* Roadstyle 0 has just 8 additional sprites for both road and rail:
     * one for the road/rail going off in each direction. */
    for (i = 0; i < num_valid_tileset_dirs; i++) {
      enum direction8 dir = valid_tileset_dirs[i];
      const char *dir_name = dir_get_tileset_name(dir);

      my_snprintf(buffer, sizeof(buffer), "r.road_%s", dir_name);
      SET_SPRITE(road.dir[i], buffer);
      my_snprintf(buffer, sizeof(buffer), "r.rail_%s", dir_name);
      SET_SPRITE(rail.dir[i], buffer);
    }
  } else if (roadstyle == 1) {
    int num_index = 1 << (num_valid_tileset_dirs / 2), j;

    /* Roadstyle 1 has 32 additional sprites for both road and rail:
     * 16 each for cardinal and diagonal directions.  Each set
     * of 16 provides a NSEW-indexed sprite to provide connectors for
     * all rails in the cardinal/diagonal directions.  The 0 entry is
     * unused (the "isolated" sprite is used instead). */

    for (i = 1; i < num_index; i++) {
      char c[64] = "", d[64] = "";

      for (j = 0; j < num_valid_tileset_dirs / 2; j++) {
	int value = (i >> j) & 1;

	snprintf(c + strlen(c), sizeof(c) - strlen(c), "%s%d",
		 dir_get_tileset_name(valid_tileset_dirs[2 * j]), value);
	snprintf(d + strlen(d), sizeof(d) - strlen(d), "%s%d",
		 dir_get_tileset_name(valid_tileset_dirs[2 * j + 1]), value);
      }

      my_snprintf(buffer, sizeof(buffer), "r.c_road_%s", c);
      SET_SPRITE(road.even[i], buffer);

      my_snprintf(buffer, sizeof(buffer), "r.d_road_%s", d);
      SET_SPRITE(road.odd[i], buffer);

      my_snprintf(buffer, sizeof(buffer), "r.c_rail_%s", c);
      SET_SPRITE(rail.even[i], buffer);

      my_snprintf(buffer, sizeof(buffer), "r.d_rail_%s", d);
      SET_SPRITE(rail.odd[i], buffer);
    }
  } else {
    /* Roadstyle 2 includes 256 sprites, one for every possibility.
     * Just go around clockwise, with all combinations. */
    for (i = 0; i < num_index_valid; i++) {
      my_snprintf(buffer, sizeof(buffer), "r.road_%s", valid_index_str(i));
      SET_SPRITE(road.total[i], buffer);

      my_snprintf(buffer, sizeof(buffer), "r.rail_%s", valid_index_str(i));
      SET_SPRITE(rail.total[i], buffer);
    }
  }

  /* Corner road/rail graphics are used by roadstyle 0 and 1. */
  if (roadstyle == 0 || roadstyle == 1) {
    for (i = 0; i < num_valid_tileset_dirs; i++) {
      enum direction8 dir = valid_tileset_dirs[i];

      if (!is_cardinal_tileset_dir(dir)) {
	my_snprintf(buffer, sizeof(buffer), "r.c_road_%s",
		    dir_get_tileset_name(dir));
	SET_SPRITE_OPT(road.corner[dir], buffer);

	my_snprintf(buffer, sizeof(buffer), "r.c_rail_%s",
		    dir_get_tileset_name(dir));
	SET_SPRITE_OPT(rail.corner[dir], buffer);
      }
    }
  }

  SET_SPRITE(explode.nuke, "explode.nuke");

  num_tiles_explode_unit = 0;
  do {
    my_snprintf(buffer, sizeof(buffer), "explode.unit_%d",
		num_tiles_explode_unit++);
  } while (sprite_exists(buffer));
  num_tiles_explode_unit--;
    
  if (num_tiles_explode_unit==0) {
    sprites.explode.unit = NULL;
  } else {
    sprites.explode.unit = fc_calloc(num_tiles_explode_unit,
				     sizeof(struct Sprite *));

    for (i = 0; i < num_tiles_explode_unit; i++) {
      my_snprintf(buffer, sizeof(buffer), "explode.unit_%d", i);
      SET_SPRITE(explode.unit[i], buffer);
    }
  }

  SET_SPRITE(unit.auto_attack,  "unit.auto_attack");
  SET_SPRITE(unit.auto_settler, "unit.auto_settler");
  SET_SPRITE(unit.auto_explore, "unit.auto_explore");
  SET_SPRITE(unit.fallout,	"unit.fallout");
  SET_SPRITE(unit.fortified,	"unit.fortified");     
  SET_SPRITE(unit.fortifying,	"unit.fortifying");     
  SET_SPRITE(unit.fortress,     "unit.fortress");
  SET_SPRITE(unit.airbase,      "unit.airbase");
  SET_SPRITE(unit.go_to,	"unit.goto");     
  SET_SPRITE(unit.irrigate,     "unit.irrigate");
  SET_SPRITE(unit.mine,	        "unit.mine");
  SET_SPRITE(unit.pillage,	"unit.pillage");
  SET_SPRITE(unit.pollution,    "unit.pollution");
  SET_SPRITE(unit.road,	        "unit.road");
  SET_SPRITE(unit.sentry,	"unit.sentry");      
  SET_SPRITE(unit.stack,	"unit.stack");
  sprites.unit.loaded = load_sprite("unit.loaded");
  SET_SPRITE(unit.transform,    "unit.transform");
  SET_SPRITE(unit.connect,      "unit.connect");
  SET_SPRITE(unit.patrol,       "unit.patrol");
  SET_SPRITE(unit.lowfuel, "unit.lowfuel");
  SET_SPRITE(unit.tired, "unit.tired");

  for(i=0; i<NUM_TILES_HP_BAR; i++) {
    my_snprintf(buffer, sizeof(buffer), "unit.hp_%d", i*10);
    SET_SPRITE(unit.hp_bar[i], buffer);
  }

  for (i = 0; i < MAX_VET_LEVELS; i++) {
    /* Veteran level sprites are optional.  For instance "green" units
     * usually have no special graphic. */
    my_snprintf(buffer, sizeof(buffer), "unit.vet_%d", i);
    sprites.unit.vet_lev[i] = load_sprite(buffer);
  }

  SET_SPRITE(city.disorder, "city.disorder");

  for(i=0; i<NUM_TILES_DIGITS; i++) {
    char buffer2[512];

    my_snprintf(buffer, sizeof(buffer), "city.size_%d", i);
    SET_SPRITE(city.size[i], buffer);
    my_snprintf(buffer2, sizeof(buffer2), "path.turns_%d", i);
    SET_SPRITE_ALT_OPT(path.turns[i], buffer2, buffer);

    if(i!=0) {
      my_snprintf(buffer, sizeof(buffer), "city.size_%d", i*10);
      SET_SPRITE(city.size_tens[i], buffer);
      my_snprintf(buffer2, sizeof(buffer2), "path.turns_%d", i * 10);
      SET_SPRITE_ALT_OPT(path.turns_tens[i], buffer2, buffer);
    }
    my_snprintf(buffer, sizeof(buffer), "city.t_food_%d", i);
    SET_SPRITE(city.tile_foodnum[i], buffer);
    my_snprintf(buffer, sizeof(buffer), "city.t_shields_%d", i);
    SET_SPRITE(city.tile_shieldnum[i], buffer);
    my_snprintf(buffer, sizeof(buffer), "city.t_trade_%d", i);
    SET_SPRITE(city.tile_tradenum[i], buffer);
  }

  SET_SPRITE(upkeep.food[0], "upkeep.food");
  SET_SPRITE(upkeep.food[1], "upkeep.food2");
  SET_SPRITE(upkeep.unhappy[0], "upkeep.unhappy");
  SET_SPRITE(upkeep.unhappy[1], "upkeep.unhappy2");
  SET_SPRITE(upkeep.gold[0], "upkeep.gold");
  SET_SPRITE(upkeep.gold[1], "upkeep.gold2");
  SET_SPRITE(upkeep.shield, "upkeep.shield");
  
  SET_SPRITE(user.attention, "user.attention");

  SET_SPRITE(tx.fallout,    "tx.fallout");
  SET_SPRITE(tx.pollution,  "tx.pollution");
  SET_SPRITE(tx.village,    "tx.village");
  SET_SPRITE(tx.fortress,   "tx.fortress");
  SET_SPRITE_ALT(tx.fortress_back, "tx.fortress_back", "tx.fortress");
  SET_SPRITE(tx.airbase,    "tx.airbase");
  SET_SPRITE(tx.fog,        "tx.fog");

  for (i = 0; i < num_index_cardinal; i++) {
    my_snprintf(buffer, sizeof(buffer), "tx.s_river_%s",
		cardinal_index_str(i));
    SET_SPRITE(tx.spec_river[i], buffer);
  }

  /* We use direction-specific irrigation and farmland graphics, if they
   * are available.  If not, we just fall back to the basic irrigation
   * graphics. */
  for (i = 0; i < num_index_cardinal; i++) {
    my_snprintf(buffer, sizeof(buffer), "tx.s_irrigation_%s",
		cardinal_index_str(i));
    SET_SPRITE_ALT(tx.irrigation[i], buffer, "tx.irrigation");
  }
  for (i = 0; i < num_index_cardinal; i++) {
    my_snprintf(buffer, sizeof(buffer), "tx.s_farmland_%s",
		cardinal_index_str(i));
    SET_SPRITE_ALT(tx.farmland[i], buffer, "tx.farmland");
  }

  switch (darkness_style) {
  case DARKNESS_NONE:
    /* Nothing. */
    break;
  case DARKNESS_ISORECT:
    {
      /* Isometric: take a single tx.darkness tile and split it into 4. */
      struct Sprite *darkness = load_sprite("tx.darkness");
      const int W = NORMAL_TILE_WIDTH, H = NORMAL_TILE_HEIGHT;
      int offsets[4][2] = {{W / 2, 0}, {0, H / 2}, {W / 2, H / 2}, {0, 0}};

      if (!darkness) {
	freelog(LOG_FATAL, "Sprite tx.darkness missing.");
	exit(EXIT_FAILURE);
      }
      for (i = 0; i < 4; i++) {
	sprites.tx.darkness[i] = crop_sprite(darkness, offsets[i][0],
					     offsets[i][1], W / 2, H / 2,
					     NULL, 0, 0);
      }
    }
    break;
  case DARKNESS_CARD_SINGLE:
    for (i = 0; i < num_cardinal_tileset_dirs; i++) {
      enum direction8 dir = cardinal_tileset_dirs[i];

      my_snprintf(buffer, sizeof(buffer), "tx.darkness_%s",
		  dir_get_tileset_name(dir));
      SET_SPRITE(tx.darkness[i], buffer);
    }
    break;
  case DARKNESS_CARD_FULL:
    for(i = 1; i < num_index_cardinal; i++) {
      my_snprintf(buffer, sizeof(buffer), "tx.darkness_%s",
		  cardinal_index_str(i));
      SET_SPRITE(tx.darkness[i], buffer);
    }
    break;
  }

  for(i=0; i<4; i++) {
    my_snprintf(buffer, sizeof(buffer), "tx.river_outlet_%c", dir_char[i]);
    SET_SPRITE(tx.river_outlet[i], buffer);
  }

  sprites.city.tile_wall = NULL;    /* no place to initialize this variable */
  sprites.city.tile = NULL;         /* no place to initialize this variable */
}

/**********************************************************************
  Load the tiles; requires tilespec_read_toplevel() called previously.
  Leads to tile_sprites being allocated and filled with pointers
  to sprites.   Also sets up and populates sprite_hash, and calls func
  to initialize 'sprites' structure.
***********************************************************************/
void tilespec_load_tiles(void)
{
  tilespec_lookup_sprite_tags();
  finish_loading_sprites();
}

/**********************************************************************
  Lookup sprite to match tag, or else to match alt if don't find,
  or else return NULL, and emit log message.
***********************************************************************/
static struct Sprite* lookup_sprite_tag_alt(const char *tag, const char *alt,
					    bool required, const char *what,
					    const char *name)
{
  struct Sprite *sp;
  
  /* (should get sprite_hash before connection) */
  if (!sprite_hash) {
    die("attempt to lookup for %s %s before sprite_hash setup", what, name);
  }

  sp = load_sprite(tag);
  if (sp) return sp;

  sp = load_sprite(alt);
  if (sp) {
    freelog(LOG_VERBOSE,
	    "Using alternate graphic %s (instead of %s) for %s %s",
	    alt, tag, what, name);
    return sp;
  }

  freelog(required ? LOG_FATAL : LOG_VERBOSE,
	  _("Don't have graphics tags %s or %s for %s %s"),
	  tag, alt, what, name);
  if (required) {
    exit(EXIT_FAILURE);
  }
  return NULL;
}

/**********************************************************************
  Set unit_type sprite value; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tilespec_setup_unit_type(int id)
{
  struct unit_type *ut = get_unit_type(id);
  
  ut->sprite = lookup_sprite_tag_alt(ut->graphic_str, ut->graphic_alt,
				     unit_type_exists(id), "unit_type",
				     ut->name);

  /* should maybe do something if NULL, eg generic default? */
}

/**********************************************************************
  Set improvement_type sprite value; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tilespec_setup_impr_type(int id)
{
  struct impr_type *pimpr = get_improvement_type(id);

  pimpr->sprite = lookup_sprite_tag_alt(pimpr->graphic_str,
					pimpr->graphic_alt,
					FALSE, "impr_type",
					pimpr->name);

  /* should maybe do something if NULL, eg generic default? */
}

/**********************************************************************
  Set tech_type sprite value; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tilespec_setup_tech_type(int id)
{
  advances[id].sprite
    = lookup_sprite_tag_alt(advances[id].graphic_str,
			    advances[id].graphic_alt,
			    FALSE, "tech_type",
			    get_tech_name(game.player_ptr, id));

  /* should maybe do something if NULL, eg generic default? */
}

/**********************************************************************
  Set tile_type sprite values; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tilespec_setup_tile_type(Terrain_type_id terrain)
{
  struct tile_type *tt = get_tile_type(terrain);
  struct terrain_drawing_data *draw;
  char buffer1[MAX_LEN_NAME + 20];
  int i, l;
  
  if (tt->terrain_name[0] == '\0') {
    return;
  }

  draw = hash_lookup_data(terrain_hash, tt->graphic_str);
  if (!draw) {
    draw = hash_lookup_data(terrain_hash, tt->graphic_alt);
    if (!draw) {
      freelog(LOG_FATAL, "No graphics %s or %s for %s terrain.",
	      tt->graphic_str, tt->graphic_alt, tt->terrain_name);
      exit(EXIT_FAILURE);
    }
  }

  /* Set up each layer of the drawing. */
  for (l = 0; l < draw->num_layers; l++) {
    sprite_vector_init(&draw->layer[l].base);
    sprite_vector_reserve(&draw->layer[l].base, 1);
    if (draw->layer[l].match_style == MATCH_NONE) {
      /* Load single sprite for this terrain. */
      for (i = 0; ; i++) {
	struct Sprite *sprite;

	my_snprintf(buffer1, sizeof(buffer1), "t.%s%d", draw->name, i + 1);
	sprite = load_sprite(buffer1);
	if (!sprite) {
	  break;
	}
	sprite_vector_reserve(&draw->layer[l].base, i + 1);
	draw->layer[l].base.p[i] = sprite;
      }
      if (i == 0) {
	/* TRANS: obscure tileset error. */
	freelog(LOG_FATAL, _("Missing base sprite tag \"%s1\"."),
		draw->name);
	exit(EXIT_FAILURE);
      }
    } else {
      switch (draw->layer[l].cell_type) {
      case CELL_SINGLE:
	/* Load 16 cardinally-matched sprites. */
	for (i = 0; i < num_index_cardinal; i++) {
	  my_snprintf(buffer1, sizeof(buffer1),
		      "t.%s_%s", draw->name, cardinal_index_str(i));
	  draw->layer[l].match[i] = lookup_sprite_tag_alt(buffer1, "", TRUE,
							  "tile_type",
							  tt->terrain_name);
	}
	draw->layer[l].base.p[0] = draw->layer[l].match[0];
	break;
      case CELL_RECT:
	{
	  const int count = draw->layer[l].match_count;
	  /* N directions (NSEW) * 3 dimensions of matching */
	  /* FIXME: should use exp() or expi() here. */
	  const int number = NUM_CORNER_DIRS * count * count * count;

	  draw->layer[l].cells
	    = fc_malloc(number * sizeof(*draw->layer[l].cells));

	  for (i = 0; i < number; i++) {
	    int value = i / NUM_CORNER_DIRS;
	    enum direction4 dir = i % NUM_CORNER_DIRS;
	    const char dirs[4] = "udrl"; /* Matches direction4 ordering */

	    switch (draw->layer[l].match_style) {
	    case MATCH_NONE:
	      assert(0); /* Impossible. */
	      break;
	    case MATCH_BOOLEAN:
	      my_snprintf(buffer1, sizeof(buffer1), "t.%s_cell_%c%d%d%d",
			  draw->name, dirs[dir],
			  (value >> 0) & 1,
			  (value >> 1) & 1,
			  (value >> 2) & 1);
	      draw->layer[l].cells[i]
		= lookup_sprite_tag_alt(buffer1, "", TRUE, "tile_type",
					tt->terrain_name);
	      break;
	    case MATCH_FULL:
	      {
		int n = 0, s = 0, e = 0, w = 0;
		int v1, v2, v3;
		int this = draw->layer[l].match_type;
		struct Sprite *sprite;

		v1 = value % count;
		value /= count;
		v2 = value % count;
		value /= count;
		v3 = value % count;

		assert(v1 < count && v2 < count && v3 < count);

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
		  e = this;
		  s = v1;
		  w = v2;
		  n = v3;
		  break;
		}
		my_snprintf(buffer1, sizeof(buffer1),
			    "t.cellgroup_%s_%s_%s_%s",
			    layers[l].match_types[n],
			    layers[l].match_types[e],
			    layers[l].match_types[s],
			    layers[l].match_types[w]);
		sprite = load_sprite(buffer1);

		if (sprite) {
		  /* Crop the sprite to separate this cell. */
		  const int W = NORMAL_TILE_WIDTH, H = NORMAL_TILE_HEIGHT;
		  int x[4] = {W / 4, W / 4, 0, W / 2};
		  int y[4] = {H / 2, 0, H / 4, H / 4};
		  int xo[4] = {0, 0, -W / 2, W / 2};
		  int yo[4] = {H / 2, -H / 2, 0, 0};

		  sprite = crop_sprite(sprite,
				       x[dir], y[dir], W / 2, H / 2,
				       sprites.black_tile, xo[dir], yo[dir]);
		}

		draw->layer[l].cells[i] = sprite;
		break;
	      }
	    }
	  }
	}
	my_snprintf(buffer1, sizeof(buffer1), "t.%s1", draw->name);
	draw->layer[l].base.p[0]
	  = lookup_sprite_tag_alt(buffer1, "", FALSE, "tile_type",
				  tt->terrain_name);
	break;
      }
    }
  }

  if (draw->is_blended && is_isometric) {
    /* Set up blending sprites. This only works in iso-view! */
    const int W = NORMAL_TILE_WIDTH, H = NORMAL_TILE_HEIGHT;
    const int offsets[4][2] = {
      {W / 2, 0}, {0, H / 2}, {W / 2, H / 2}, {0, 0}
    };
    enum direction4 dir;

    for (dir = 0; dir < 4; dir++) {
      assert(sprite_vector_size(&draw->layer[0].base) > 0);
      draw->blend[dir] = crop_sprite(draw->layer[0].base.p[0],
				     offsets[dir][0], offsets[dir][1],
				     W / 2, H / 2,
				     sprites.dither_tile, 0, 0);
    }
  }

  for (i=0; i<2; i++) {
    const char *name = (i != 0) ? tt->special_2_name : tt->special_1_name;
    if (name[0] != '\0') {
      draw->special[i]
	= lookup_sprite_tag_alt(tt->special[i].graphic_str,
				tt->special[i].graphic_alt,
				TRUE, "tile_type special", name);
      assert(draw->special[i] != NULL);
    } else {
      draw->special[i] = NULL;
    }
    /* should probably do something if NULL, eg generic default? */
  }

  if (draw->mine_tag) {
    draw->mine = load_sprite(draw->mine_tag);
  } else {
    draw->mine = NULL;
  }

  sprites.terrain[terrain] = draw;
}

/**********************************************************************
  Set government sprite value; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tilespec_setup_government(int id)
{
  struct government *gov = get_government(id);
  
  gov->sprite = lookup_sprite_tag_alt(gov->graphic_str, gov->graphic_alt,
				      TRUE, "government", gov->name);
  
  /* should probably do something if NULL, eg generic default? */
}

/**********************************************************************
  Set nation flag sprite value; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tilespec_setup_nation_flag(int id)
{
  struct nation_type *nation = get_nation_by_idx(id);
  char *tags[] = {nation->flag_graphic_str,
		  nation->flag_graphic_alt,
		  "f.unknown", NULL};
  int i;

  nation->flag_sprite = NULL;
  for (i = 0; tags[i] && !nation->flag_sprite; i++) {
    nation->flag_sprite = load_sprite(tags[i]);
  }
  if (!nation->flag_sprite) {
    /* Should never get here because of the f.unknown fallback. */
    freelog(LOG_FATAL, "No national flag for %s.", nation->name);
    exit(EXIT_FAILURE);
  }
}

/**********************************************************************
  ...
***********************************************************************/
static struct Sprite *get_city_nation_flag_sprite(struct city *pcity)
{
  return get_nation_by_plr(city_owner(pcity))->flag_sprite;
}

/**********************************************************************
 ...
***********************************************************************/
static struct Sprite *get_unit_nation_flag_sprite(struct unit *punit)
{
  return get_nation_by_plr(unit_owner(punit))->flag_sprite;
}

/**************************************************************************
Return the sprite needed to draw the city
**************************************************************************/
static struct Sprite *get_city_sprite(struct city *pcity)
{
  int size, style;

  style = get_city_style(pcity);    /* get style and match the best tile */
                                    /* based on city size                */
  for( size=0; size < city_styles[style].tiles_num; size++)
    if( pcity->size < city_styles[style].tresh[size]) 
      break;

  if (is_isometric) {
    if (city_got_citywalls(pcity))
      return sprites.city.tile_wall[style][size-1];
    else
      return sprites.city.tile[style][size-1];
  } else {
    return sprites.city.tile[style][size-1];
  }
}

/**************************************************************************
Return the sprite needed to draw the city wall
Not used for isometric view.
**************************************************************************/
static struct Sprite *get_city_wall_sprite(struct city *pcity)
{
  int style = get_city_style(pcity);

  return sprites.city.tile[style][city_styles[style].tiles_num];
}

/**************************************************************************
Return the sprite needed to draw the occupied tile
**************************************************************************/
static struct Sprite *get_city_occupied_sprite(struct city *pcity)
{
  int style = get_city_style(pcity);

  return sprites.city.tile[style][city_styles[style].tiles_num+1];
}

#define ADD_SPRITE(s, draw_style, draw_fog, x_offset, y_offset)	\
  (assert(s != NULL),						\
   sprs->type = DRAWN_SPRITE,					\
   sprs->data.sprite.style = draw_style,			\
   sprs->data.sprite.sprite = s,				\
   sprs->data.sprite.foggable = (draw_fog && fogstyle == 0),	\
   sprs->data.sprite.offset_x = x_offset,			\
   sprs->data.sprite.offset_y = y_offset,			\
   sprs++)
#define ADD_SPRITE_SIMPLE(s) ADD_SPRITE(s, DRAW_NORMAL, TRUE, 0, 0)
#define ADD_SPRITE_FULL(s) ADD_SPRITE(s, DRAW_FULL, TRUE, 0, 0)

#define ADD_GRID(ptile, mode)						    \
  (sprs->type = DRAWN_GRID,						    \
   sprs->data.grid.tile = (ptile),					    \
   sprs->data.grid.citymode = (mode),					    \
   sprs++)

#define ADD_BG(bg_color)						    \
  (sprs->type = DRAWN_BG,						    \
   sprs->data.bg.color = (bg_color),					    \
   sprs++)

/**************************************************************************
  Assemble some data that is used in building the tile sprite arrays.
    (map_x, map_y) : the (normalized) map position
  The values we fill in:
    ttype          : the terrain type of the tile
    tspecial       : all specials the tile has
    ttype_near     : terrain types of all adjacent terrain
    tspecial_near  : specials of all adjacent terrain
**************************************************************************/
static void build_tile_data(struct tile *ptile,
			    Terrain_type_id *ttype,
			    enum tile_special_type *tspecial,
			    Terrain_type_id *ttype_near,
			    enum tile_special_type *tspecial_near)
{
  enum direction8 dir;

  *tspecial = map_get_special(ptile);
  *ttype = map_get_terrain(ptile);

  /* Loop over all adjacent tiles.  We should have an iterator for this. */
  for (dir = 0; dir < 8; dir++) {
    struct tile *tile1 = mapstep(ptile, dir);

    if (tile1 && tile_get_known(tile1) != TILE_UNKNOWN) {
      tspecial_near[dir] = map_get_special(tile1);
      ttype_near[dir] = map_get_terrain(tile1);
    } else {
      /* We draw the edges of the (known) map as if the same terrain just
       * continued off the edge of the map. */
      tspecial_near[dir] = S_NO_SPECIAL;
      ttype_near[dir] = *ttype;
    }
  }
}

/**********************************************************************
  Fill in the sprite array for the unit
***********************************************************************/
static int fill_unit_sprite_array(struct drawn_sprite *sprs,
				  struct unit *punit,
				  bool stack, bool backdrop)
{
  struct drawn_sprite *save_sprs = sprs;
  int ihp;

  if (backdrop) {
    if (!solid_color_behind_units) {
      ADD_SPRITE(get_unit_nation_flag_sprite(punit),
		 DRAW_FULL, TRUE, flag_offset_x, flag_offset_y);
    } else {
      ADD_BG(player_color(unit_owner(punit)));
    }
  }

  ADD_SPRITE_FULL(unit_type(punit)->sprite);

  if (sprites.unit.loaded && punit->transported_by != -1) {
    ADD_SPRITE_FULL(sprites.unit.loaded);
  }

  if(punit->activity!=ACTIVITY_IDLE) {
    struct Sprite *s = NULL;
    switch(punit->activity) {
    case ACTIVITY_MINE:
      s = sprites.unit.mine;
      break;
    case ACTIVITY_POLLUTION:
      s = sprites.unit.pollution;
      break;
    case ACTIVITY_FALLOUT:
      s = sprites.unit.fallout;
      break;
    case ACTIVITY_PILLAGE:
      s = sprites.unit.pillage;
      break;
    case ACTIVITY_ROAD:
    case ACTIVITY_RAILROAD:
      s = sprites.unit.road;
      break;
    case ACTIVITY_IRRIGATE:
      s = sprites.unit.irrigate;
      break;
    case ACTIVITY_EXPLORE:
      s = sprites.unit.auto_explore;
      break;
    case ACTIVITY_FORTIFIED:
      s = sprites.unit.fortified;
      break;
    case ACTIVITY_FORTIFYING:
      s = sprites.unit.fortifying;
      break;
    case ACTIVITY_FORTRESS:
      s = sprites.unit.fortress;
      break;
    case ACTIVITY_AIRBASE:
      s = sprites.unit.airbase;
      break;
    case ACTIVITY_SENTRY:
      s = sprites.unit.sentry;
      break;
    case ACTIVITY_GOTO:
      s = sprites.unit.go_to;
      break;
    case ACTIVITY_TRANSFORM:
      s = sprites.unit.transform;
      break;
    default:
      break;
    }

    ADD_SPRITE_FULL(s);
  }

  if (punit->ai.control && punit->activity != ACTIVITY_EXPLORE) {
    if (is_military_unit(punit)) {
      ADD_SPRITE_FULL(sprites.unit.auto_attack);
    } else {
      ADD_SPRITE_FULL(sprites.unit.auto_settler);
    }
  }

  if (unit_has_orders(punit)) {
    if (punit->orders.repeat) {
      ADD_SPRITE_FULL(sprites.unit.patrol);
    } else if (punit->activity != ACTIVITY_IDLE) {
      ADD_SPRITE_SIMPLE(sprites.unit.connect);
    } else {
      ADD_SPRITE_FULL(sprites.unit.go_to);
    }
  }

  if (sprites.unit.lowfuel
      && unit_type(punit)->fuel > 0
      && punit->fuel == 1
      && punit->moves_left <= 2 * SINGLE_MOVE) {
    /* Show a low-fuel graphic if the plane has 2 or fewer moves left. */
    ADD_SPRITE_FULL(sprites.unit.lowfuel);
  }
  if (sprites.unit.tired
      && punit->moves_left < SINGLE_MOVE) {
    /* Show a "tired" graphic if the unit has fewer than one move
     * remaining. */
    ADD_SPRITE_FULL(sprites.unit.tired);
  }

  if (stack || punit->occupy) {
    ADD_SPRITE_FULL(sprites.unit.stack);
  }

  if (sprites.unit.vet_lev[punit->veteran]) {
    ADD_SPRITE_FULL(sprites.unit.vet_lev[punit->veteran]);
  }

  ihp = ((NUM_TILES_HP_BAR-1)*punit->hp) / unit_type(punit)->hp;
  ihp = CLIP(0, ihp, NUM_TILES_HP_BAR-1);
  ADD_SPRITE_FULL(sprites.unit.hp_bar[ihp]);

  return sprs - save_sprs;
}

/**************************************************************************
  Add any corner road sprites to the sprite array.
**************************************************************************/
static int fill_road_corner_sprites(struct drawn_sprite *sprs,
				    bool road, bool *road_near,
				    bool rail, bool *rail_near)
{
  struct drawn_sprite *saved_sprs = sprs;
  int i;

  assert(draw_roads_rails);

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
  for (i = 0; i < num_valid_tileset_dirs; i++) {
    enum direction8 dir = valid_tileset_dirs[i];

    if (!is_cardinal_tileset_dir(dir)) {
      /* Draw corner sprites for this non-cardinal direction. */
      int cw = (i + 1) % num_valid_tileset_dirs;
      int ccw = (i + num_valid_tileset_dirs - 1) % num_valid_tileset_dirs;
      enum direction8 dir_cw = valid_tileset_dirs[cw];
      enum direction8 dir_ccw = valid_tileset_dirs[ccw];

      if (sprites.road.corner[dir]
	  && (road_near[dir_cw] && road_near[dir_ccw]
	      && !(rail_near[dir_cw] && rail_near[dir_ccw]))
	  && !(road && road_near[dir] && !(rail && rail_near[dir]))) {
	ADD_SPRITE_SIMPLE(sprites.road.corner[dir]);
      }
    }
  }

  return sprs - saved_sprs;
}

/**************************************************************************
  Add any corner rail sprites to the sprite array.
**************************************************************************/
static int fill_rail_corner_sprites(struct drawn_sprite *sprs,
				    bool rail, bool *rail_near)
{
  struct drawn_sprite *saved_sprs = sprs;
  int i;

  assert(draw_roads_rails);

  /* Rails going diagonally adjacent to this tile need to be
   * partly drawn on this tile. */

  for (i = 0; i < num_valid_tileset_dirs; i++) {
    enum direction8 dir = valid_tileset_dirs[i];

    if (!is_cardinal_tileset_dir(dir)) {
      /* Draw corner sprites for this non-cardinal direction. */
      int cw = (i + 1) % num_valid_tileset_dirs;
      int ccw = (i + num_valid_tileset_dirs - 1) % num_valid_tileset_dirs;
      enum direction8 dir_cw = valid_tileset_dirs[cw];
      enum direction8 dir_ccw = valid_tileset_dirs[ccw];

      if (sprites.rail.corner[dir]
	  && rail_near[dir_cw] && rail_near[dir_ccw]
	  && !(rail && rail_near[dir])) {
	ADD_SPRITE_SIMPLE(sprites.rail.corner[dir]);
      }
    }
  }

  return sprs - saved_sprs;
}

/**************************************************************************
  Fill all road and rail sprites into the sprite array.
**************************************************************************/
static int fill_road_rail_sprite_array(struct drawn_sprite *sprs,
				       enum tile_special_type tspecial,
				       enum tile_special_type *tspecial_near,
				       struct city *pcity)
{
  struct drawn_sprite *saved_sprs = sprs;
  bool road, road_near[8], rail, rail_near[8];
  bool draw_road[8], draw_single_road, draw_rail[8], draw_single_rail;
  enum direction8 dir;

  if (!draw_roads_rails) {
    /* Don't draw anything. */
    return 0;
  }

  /* Fill some data arrays. rail_near and road_near store whether road/rail
   * is present in the given direction.  draw_rail and draw_road store
   * whether road/rail is to be drawn in that direction.  draw_single_road
   * and draw_single_rail store whether we need an isolated road/rail to be
   * drawn. */
  road = contains_special(tspecial, S_ROAD);
  rail = contains_special(tspecial, S_RAILROAD);
  draw_single_road = road && (!pcity || !draw_cities) && !rail;
  draw_single_rail = rail && (!pcity || !draw_cities);
  for (dir = 0; dir < 8; dir++) {
    /* Check if there is adjacent road/rail. */
    road_near[dir] = contains_special(tspecial_near[dir], S_ROAD);
    rail_near[dir] = contains_special(tspecial_near[dir], S_RAILROAD);

    /* Draw rail/road if there is a connection from this tile to the
     * adjacent tile.  But don't draw road if there is also a rail
     * connection. */
    draw_rail[dir] = rail && rail_near[dir];
    draw_road[dir] = road && road_near[dir] && !draw_rail[dir];

    /* Don't draw an isolated road/rail if there's any connection. */
    draw_single_rail &= !draw_rail[dir];
    draw_single_road &= !draw_rail[dir] && !draw_road[dir];
  }

  /* Draw road corners underneath rails (styles 0 and 1). */
  sprs += fill_road_corner_sprites(sprs, road, road_near, rail, rail_near);

  if (roadstyle == 0) {
    /* With roadstyle 0, we simply draw one road/rail for every connection.
     * This means we only need a few sprites, but a lot of drawing is
     * necessary and it generally doesn't look very good. */
    int i;

    /* First raw roads under rails. */
    if (road) {
      for (i = 0; i < num_valid_tileset_dirs; i++) {
	if (draw_road[valid_tileset_dirs[i]]) {
	  ADD_SPRITE_SIMPLE(sprites.road.dir[i]);
	}
      }
    }

    /* Then draw rails over roads. */
    if (rail) {
      for (i = 0; i < num_valid_tileset_dirs; i++) {
	if (draw_rail[valid_tileset_dirs[i]]) {
	  ADD_SPRITE_SIMPLE(sprites.rail.dir[i]);
	}
      }
    }
  } else if (roadstyle == 1) {
    /* With roadstyle 1, we draw one sprite for cardinal road connections,
     * one sprite for diagonal road connections, and the same for rail.
     * This means we need about 4x more sprites than in style 0, but up to
     * 4x less drawing is needed.  The drawing quality may also be
     * improved. */

    /* First draw roads under rails. */
    if (road) {
      int road_even_tileno = 0, road_odd_tileno = 0, i;

      for (i = 0; i < num_valid_tileset_dirs / 2; i++) {
	enum direction8 even = valid_tileset_dirs[2 * i];
	enum direction8 odd = valid_tileset_dirs[2 * i + 1];

	if (draw_road[even]) {
	  road_even_tileno |= 1 << i;
	}
	if (draw_road[odd]) {
	  road_odd_tileno |= 1 << i;
	}
      }

      /* Draw the cardinal/even roads first. */
      if (road_even_tileno != 0) {
	ADD_SPRITE_SIMPLE(sprites.road.even[road_even_tileno]);
      }
      if (road_odd_tileno != 0) {
	ADD_SPRITE_SIMPLE(sprites.road.odd[road_odd_tileno]);
      }
    }

    /* Then draw rails over roads. */
    if (rail) {
      int rail_even_tileno = 0, rail_odd_tileno = 0, i;

      for (i = 0; i < num_valid_tileset_dirs / 2; i++) {
	enum direction8 even = valid_tileset_dirs[2 * i];
	enum direction8 odd = valid_tileset_dirs[2 * i + 1];

	if (draw_rail[even]) {
	  rail_even_tileno |= 1 << i;
	}
	if (draw_rail[odd]) {
	  rail_odd_tileno |= 1 << i;
	}
      }

      /* Draw the cardinal/even rails first. */
      if (rail_even_tileno != 0) {
	ADD_SPRITE_SIMPLE(sprites.rail.even[rail_even_tileno]);
      }
      if (rail_odd_tileno != 0) {
	ADD_SPRITE_SIMPLE(sprites.rail.odd[rail_odd_tileno]);
      }
    }
  } else {
    /* Roadstyle 2 is a very simple method that lets us simply retrieve 
     * entire finished tiles, with a bitwise index of the presence of
     * roads in each direction. */

    /* Draw roads first */
    if (road) {
      int road_tileno = 0, i;

      for (i = 0; i < num_valid_tileset_dirs; i++) {
	enum direction8 dir = valid_tileset_dirs[i];

	if (draw_road[dir]) {
	  road_tileno |= 1 << i;
	}
      }

      if (road_tileno != 0 || draw_single_road) {
        ADD_SPRITE_SIMPLE(sprites.road.total[road_tileno]);
      }
    }

    /* Then draw rails over roads. */
    if (rail) {
      int rail_tileno = 0, i;

      for (i = 0; i < num_valid_tileset_dirs; i++) {
	enum direction8 dir = valid_tileset_dirs[i];

	if (draw_rail[dir]) {
	  rail_tileno |= 1 << i;
	}
      }

      if (rail_tileno != 0 || draw_single_rail) {
        ADD_SPRITE_SIMPLE(sprites.rail.total[rail_tileno]);
      }
    }
  }

  /* Draw isolated rail/road separately (styles 0 and 1 only). */
  if (roadstyle == 0 || roadstyle == 1) { 
    if (draw_single_rail) {
      ADD_SPRITE_SIMPLE(sprites.rail.isolated);
    } else if (draw_single_road) {
      ADD_SPRITE_SIMPLE(sprites.road.isolated);
    }
  }

  /* Draw rail corners over roads (styles 0 and 1). */
  sprs += fill_rail_corner_sprites(sprs, rail, rail_near);

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
static int get_irrigation_index(enum tile_special_type *tspecial_near)
{
  int tileno = 0, i;

  for (i = 0; i < num_cardinal_tileset_dirs; i++) {
    enum direction8 dir = cardinal_tileset_dirs[i];

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
static int fill_irrigation_sprite_array(struct drawn_sprite *sprs,
					enum tile_special_type tspecial,
					enum tile_special_type *tspecial_near,
					struct city *pcity)
{
  struct drawn_sprite *saved_sprs = sprs;

  /* Tiles with S_FARMLAND also have S_IRRIGATION set. */
  assert(!contains_special(tspecial, S_FARMLAND)
	 || contains_special(tspecial, S_IRRIGATION));

  /* We don't draw the irrigation if there's a city (it just gets overdrawn
   * anyway, and ends up looking bad). */
  if (draw_irrigation
      && contains_special(tspecial, S_IRRIGATION)
      && !(pcity && draw_cities)) {
    int index = get_irrigation_index(tspecial_near);

    if (contains_special(tspecial, S_FARMLAND)) {
      ADD_SPRITE_SIMPLE(sprites.tx.farmland[index]);
    } else {
      ADD_SPRITE_SIMPLE(sprites.tx.irrigation[index]);
    }
  }

  return sprs - saved_sprs;
}

/****************************************************************************
  Fill in the sprite array for blended terrain.
****************************************************************************/
static int fill_blending_sprite_array(struct drawn_sprite *sprs,
				      struct tile *ptile,
				      Terrain_type_id *ttype_near)
{
  struct drawn_sprite *saved_sprs = sprs;
  Terrain_type_id ttype = map_get_terrain(ptile);

  if (is_isometric && sprites.terrain[ttype]->is_blended) {
    enum direction4 dir;
    const int W = NORMAL_TILE_WIDTH, H = NORMAL_TILE_HEIGHT;
    const int offsets[4][2] = {
      {W/2, 0}, {0, H / 2}, {W / 2, H / 2}, {0, 0}
    };

    /*
     * We want to mark unknown tiles so that an unreal tile will be
     * given the same marking as our current tile - that way we won't
     * get the "unknown" dither along the edge of the map.
     */
    for (dir = 0; dir < 4; dir++) {
      struct tile *tile1 = mapstep(ptile, DIR4_TO_DIR8[dir]);
      Terrain_type_id other = ttype_near[DIR4_TO_DIR8[dir]];

      if (!tile1
	  || tile_get_known(tile1) == TILE_UNKNOWN
	  || other == ttype
	  || !sprites.terrain[other]->is_blended) {
	continue;
      }

      ADD_SPRITE(sprites.terrain[other]->blend[dir], DRAW_NORMAL, TRUE,
		 offsets[dir][0], offsets[dir][1]);
    }
  }

  return sprs - saved_sprs;
}

/****************************************************************************
  Add sprites for the base terrain to the sprite list.  This doesn't
  include specials or rivers.
****************************************************************************/
static int fill_terrain_sprite_array(struct drawn_sprite *sprs,
				     struct tile *ptile,
				     Terrain_type_id *ttype_near)
{
  struct drawn_sprite *saved_sprs = sprs;
  struct Sprite *sprite;
  Terrain_type_id ttype = ptile->terrain;
  struct terrain_drawing_data *draw = sprites.terrain[ttype];
  int l, i, tileno;
  struct tile *adjc_tile;

  if (!draw_terrain) {
    return 0;
  }

  /* Skip the normal drawing process. */
  if (ptile->spec_sprite && (sprite = load_sprite(ptile->spec_sprite))) {
    ADD_SPRITE_SIMPLE(sprite);
    return 1;
  }

  for (l = 0; l < draw->num_layers; l++) {
    if (draw->layer[l].match_style == MATCH_NONE) {
      int count = sprite_vector_size(&draw->layer[l].base);

      /* Pseudo-random reproducable algorithm to pick a sprite. */
#define LARGE_PRIME 10007
#define SMALL_PRIME 1009
      assert(count < SMALL_PRIME);
      assert((int)(LARGE_PRIME * MAX_MAP_INDEX) > 0);
      count = ((ptile->index
		* LARGE_PRIME) % SMALL_PRIME) % count;
      ADD_SPRITE(draw->layer[l].base.p[count],
		 draw->layer[l].is_tall ? DRAW_FULL : DRAW_NORMAL,
		 TRUE, draw->layer[l].offset_x, draw->layer[l].offset_y);
    } else {
      int match_type = draw->layer[l].match_type;

#define MATCH(dir)                                               \
      (sprites.terrain[ttype_near[(dir)]]->num_layers > l	 \
       ? sprites.terrain[ttype_near[(dir)]]->layer[l].match_type : -1)

      if (draw->layer[l].cell_type == CELL_SINGLE) {
	tileno = 0;
	assert(draw->layer[l].match_style == MATCH_BOOLEAN);
	for (i = 0; i < num_cardinal_tileset_dirs; i++) {
	  enum direction8 dir = cardinal_tileset_dirs[i];

	  if (MATCH(dir) == match_type) {
	    tileno |= 1 << i;
	  }
	}

	ADD_SPRITE(draw->layer[l].match[tileno],
		   draw->layer[l].is_tall ? DRAW_FULL : DRAW_NORMAL,
		   TRUE, draw->layer[l].offset_x, draw->layer[l].offset_y);
      } else if (draw->layer[l].cell_type == CELL_RECT) {
	/* Divide the tile up into four rectangular cells.  Now each of these
	 * cells covers one corner, and each is adjacent to 3 different
	 * tiles.  For each cell we pixk a sprite based upon the adjacent
	 * terrains at each of those tiles.  Thus we have 8 different sprites
	 * for each of the 4 cells (32 sprites total).
	 *
	 * These arrays correspond to the direction4 ordering. */
	const int W = NORMAL_TILE_WIDTH, H = NORMAL_TILE_HEIGHT;
	const int iso_offsets[4][2] = {
	  {W / 4, 0}, {W / 4, H / 2}, {W / 2, H / 4}, {0, H / 4}
	};
	const int noniso_offsets[4][2] = {
	  {0, 0}, {W / 2, H / 2}, {W / 2, 0}, {0, H / 2}
	};
	int i;

	/* put corner cells */
	for (i = 0; i < NUM_CORNER_DIRS; i++) {
	  const int count = draw->layer[l].match_count;
	  int array_index = 0;
	  enum direction8 dir = dir_ccw(DIR4_TO_DIR8[i]);
	  int x = (is_isometric ? iso_offsets[i][0] : noniso_offsets[i][0]);
	  int y = (is_isometric ? iso_offsets[i][1] : noniso_offsets[i][1]);
	  int m[3] = {MATCH(dir_ccw(dir)), MATCH(dir), MATCH(dir_cw(dir))};
	  struct Sprite *s;

         switch (draw->layer[l].match_style) {
	 case MATCH_NONE:
	   /* Impossible */
	   assert(0);
	   break;
	 case MATCH_BOOLEAN:
	   assert(count == 2);
	   array_index = array_index * count + (m[2] != match_type);
	   array_index = array_index * count + (m[1] != match_type);
	   array_index = array_index * count + (m[0] != match_type);
	   break;
	 case MATCH_FULL:
	   if (m[0] == -1 || m[1] == -1 || m[2] == -1) {
	     break;
	   }
	   array_index = array_index * count + m[2];
	   array_index = array_index * count + m[1];
	   array_index = array_index * count + m[0];
	   break;
	 }
	 array_index = array_index * NUM_CORNER_DIRS + i;

	 s = draw->layer[l].cells[array_index];
	 if (s) {
	   ADD_SPRITE(s, DRAW_NORMAL, TRUE, x, y);
	 }
	}
      }
#undef MATCH
    }

    /* Add blending on top of the first layer. */
    if (l == 0 && draw->is_blended) {
      sprs += fill_blending_sprite_array(sprs, ptile, ttype_near);
    }

    /* Add darkness on top of the first layer.  Note that darkness is always
     * drawn, even in citymode, etc. */
    if (l == 0) {
#define UNKNOWN(dir)                                        \
      ((adjc_tile = mapstep(ptile, (dir)))		    \
       && tile_get_known(adjc_tile) == TILE_UNKNOWN)

      switch (darkness_style) {
      case DARKNESS_NONE:
	break;
      case DARKNESS_ISORECT:
	for (i = 0; i < 4; i++) {
	  const int W = NORMAL_TILE_WIDTH, H = NORMAL_TILE_HEIGHT;
	  int offsets[4][2] = {{W / 2, 0}, {0, H / 2}, {W / 2, H / 2}, {0, 0}};

	  if (UNKNOWN(DIR4_TO_DIR8[i])) {
	    ADD_SPRITE(sprites.tx.darkness[i], DRAW_NORMAL, TRUE,
		       offsets[i][0], offsets[i][1]);
	  }
	}
	break;
      case DARKNESS_CARD_SINGLE:
	for (i = 0; i < num_cardinal_tileset_dirs; i++) {
	  if (UNKNOWN(cardinal_tileset_dirs[i])) {
	    ADD_SPRITE_SIMPLE(sprites.tx.darkness[i]);
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
	for (i = 0; i < num_cardinal_tileset_dirs; i++) {
	  if (UNKNOWN(cardinal_tileset_dirs[i])) {
	    tileno |= 1 << i;
	  }
	}

	if (tileno != 0) {
	  ADD_SPRITE_SIMPLE(sprites.tx.darkness[tileno]);
	}
	break;
      }
#undef UNKNOWN
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
  generally be ptile->city; otherwise it can be any city.

  citymode specifies whether this is part of a citydlg.  If so some drawing
  is done differently.
****************************************************************************/
int fill_sprite_array(struct drawn_sprite *sprs, struct tile *ptile,
		      struct unit *punit, struct city *pcity,
		      bool citymode)
{
  Terrain_type_id ttype, ttype_near[8];
  enum tile_special_type tspecial = S_NO_SPECIAL, tspecial_near[8];
  int tileno, dir;
  struct unit *pfocus = get_unit_in_focus();
  struct drawn_sprite *save_sprs = sprs;

  /* Unit drawing is disabled if the view options is turned off, but only
   * if we're drawing on the mapview. */
  bool do_draw_unit = (punit && (draw_units || !ptile
				 || (draw_focus_unit && pfocus == punit)));

  if (ptile && tile_get_known(ptile) == TILE_UNKNOWN) {
    return sprs - save_sprs;
  }

  /* Set up background color. */
  if (solid_color_behind_units) {
    if (do_draw_unit) {
      ADD_BG(player_color(unit_owner(punit)));
    } else if (pcity && draw_cities) {
      ADD_BG(player_color(city_owner(pcity)));
    }
  } else if (!draw_terrain) {
    if (ptile) {
      ADD_BG(COLOR_STD_BACKGROUND);
    }
  }

  /* Terrain and specials. */
  if (ptile) {
    build_tile_data(ptile,
		    &ttype, &tspecial, ttype_near, tspecial_near);

    sprs += fill_terrain_sprite_array(sprs, ptile, ttype_near);

    if (is_ocean(ttype) && draw_terrain) {
      for (dir = 0; dir < 4; dir++) {
	if (contains_special(tspecial_near[DIR4_TO_DIR8[dir]], S_RIVER)) {
	  ADD_SPRITE_SIMPLE(sprites.tx.river_outlet[dir]);
	}
      }
    }

    sprs += fill_irrigation_sprite_array(sprs, tspecial, tspecial_near,
					 pcity);

    if (draw_terrain && contains_special(tspecial, S_RIVER)) {
      int i;

      /* Draw rivers on top of irrigation. */
      tileno = 0;
      for (i = 0; i < num_cardinal_tileset_dirs; i++) {
	enum direction8 dir = cardinal_tileset_dirs[i];

	if (contains_special(tspecial_near[dir], S_RIVER)
	    || is_ocean(ttype_near[dir])) {
	  tileno |= 1 << i;
	}
      }
      ADD_SPRITE_SIMPLE(sprites.tx.spec_river[tileno]);
    }
  
    sprs += fill_road_rail_sprite_array(sprs,
					tspecial, tspecial_near, pcity);

    if (draw_specials) {
      if (contains_special(tspecial, S_SPECIAL_1)) {
	ADD_SPRITE_SIMPLE(sprites.terrain[ttype]->special[0]);
      } else if (contains_special(tspecial, S_SPECIAL_2)) {
	ADD_SPRITE_SIMPLE(sprites.terrain[ttype]->special[1]);
      }
    }

    if (draw_fortress_airbase && contains_special(tspecial, S_FORTRESS)
	&& sprites.tx.fortress_back) {
      ADD_SPRITE_FULL(sprites.tx.fortress_back);
    }

    if (draw_mines && contains_special(tspecial, S_MINE)
	&& sprites.terrain[ttype]->mine) {
      ADD_SPRITE_SIMPLE(sprites.terrain[ttype]->mine);
    }
    
    if (draw_specials && contains_special(tspecial, S_HUT)) {
      ADD_SPRITE_SIMPLE(sprites.tx.village);
    }
  }

  if (ptile && is_isometric) {
    /* Add grid.  In classic view this is done later. */
    ADD_GRID(ptile, citymode);
  }

  /* City.  Some city sprites are drawn later. */
  if (pcity && draw_cities) {
    if (!solid_color_behind_units) {
      ADD_SPRITE(get_city_nation_flag_sprite(pcity),
		 DRAW_FULL, TRUE, flag_offset_x, flag_offset_y);
    } else {
      ADD_BG(player_color(city_owner(pcity)));
    }
    ADD_SPRITE_FULL(get_city_sprite(pcity));
    if (pcity->client.occupied) {
      ADD_SPRITE_FULL(get_city_occupied_sprite(pcity));
    }
    if (!is_isometric && city_got_citywalls(pcity)) {
      /* In iso-view the city wall is a part of the city sprite. */
      ADD_SPRITE_SIMPLE(get_city_wall_sprite(pcity));
    }
    if (pcity->client.unhappy) {
      ADD_SPRITE_FULL(sprites.city.disorder);
    }
  }

  if (ptile) {
    if (draw_fortress_airbase && contains_special(tspecial, S_AIRBASE)) {
      ADD_SPRITE_FULL(sprites.tx.airbase);
    }

    if (draw_pollution && contains_special(tspecial, S_POLLUTION)) {
      ADD_SPRITE_SIMPLE(sprites.tx.pollution);
    }
    if (draw_pollution && contains_special(tspecial, S_FALLOUT)) {
      ADD_SPRITE_SIMPLE(sprites.tx.fallout);
    }
  }

  if (fogstyle == 1 && draw_fog_of_war
      && ptile && tile_get_known(ptile) == TILE_KNOWN_FOGGED) {
    /* With fogstyle 1, fog is done this way. */
    ADD_SPRITE_SIMPLE(sprites.tx.fog);
  }

  /* City size.  Drawing this under fog makes it hard to read. */
  if (pcity && draw_cities) {
    if (pcity->size >= 10) {
      ADD_SPRITE(sprites.city.size_tens[pcity->size / 10], DRAW_FULL,
		 FALSE, 0, 0);
    }
    ADD_SPRITE(sprites.city.size[pcity->size % 10], DRAW_FULL,
	       FALSE, 0, 0);
  }

  if (do_draw_unit) {
    bool stacked = ptile && (unit_list_size(&ptile->units) > 1);
    bool backdrop = !pcity;

    sprs += fill_unit_sprite_array(sprs, punit, stacked, backdrop);
  }

  if (ptile) {
    if (is_isometric && draw_fortress_airbase
	&& contains_special(tspecial, S_FORTRESS)) {
      /* Draw fortress front in iso-view (non-iso view only has a fortress
       * back). */
      ADD_SPRITE_FULL(sprites.tx.fortress);
    }
  }

  if (ptile && !is_isometric) {
    /* Add grid.  In iso-view this is done earlier. */
    ADD_GRID(ptile, citymode);
  }

  return sprs - save_sprs;
}

/**********************************************************************
  Set city tiles sprite values; should only happen after
  tilespec_load_tiles().
***********************************************************************/
static void tilespec_setup_style_tile(int style, char *graphics)
{
  struct Sprite *sp;
  char buffer[128];
  int j;
  struct Sprite *sp_wall = NULL;
  char buffer_wall[128];

  city_styles[style].tiles_num = 0;

  for(j=0; j<32 && city_styles[style].tiles_num < MAX_CITY_TILES; j++) {
    my_snprintf(buffer, sizeof(buffer), "%s_%d", graphics, j);
    sp = load_sprite(buffer);
    if (is_isometric) {
      my_snprintf(buffer, sizeof(buffer_wall), "%s_%d_wall", graphics, j);
      sp_wall = load_sprite(buffer);
    }
    if (sp) {
      sprites.city.tile[style][city_styles[style].tiles_num] = sp;
      if (is_isometric) {
	assert(sp_wall != NULL);
	sprites.city.tile_wall[style][city_styles[style].tiles_num] = sp_wall;
      }
      city_styles[style].tresh[city_styles[style].tiles_num] = j;
      city_styles[style].tiles_num++;
      freelog(LOG_DEBUG, "Found tile %s_%d", graphics, j);
    }
  }

  if(city_styles[style].tiles_num == 0)      /* don't waste more time */
    return;

  if (!is_isometric) {
    /* the wall tile */
    my_snprintf(buffer, sizeof(buffer), "%s_wall", graphics);
    sp = load_sprite(buffer);
    if (sp) {
      sprites.city.tile[style][city_styles[style].tiles_num] = sp;
    } else {
      freelog(LOG_NORMAL, "Warning: no wall tile for graphic %s", graphics);
    }
  }

  /* occupied tile */
  my_snprintf(buffer, sizeof(buffer), "%s_occupied", graphics);
  sp = load_sprite(buffer);
  if (sp) {
    sprites.city.tile[style][city_styles[style].tiles_num+1] = sp;
  } else {
    freelog(LOG_NORMAL, "Warning: no occupied tile for graphic %s", graphics);
  }
}

/**********************************************************************
  Set city tiles sprite values; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tilespec_setup_city_tiles(int style)
{
  tilespec_setup_style_tile(style, city_styles[style].graphic);

  if (city_styles[style].tiles_num == 0) {
    /* no tiles found, try alternate */
    freelog(LOG_NORMAL, "No tiles for %s style, trying alternate %s style",
            city_styles[style].graphic, city_styles[style].graphic_alt);

    tilespec_setup_style_tile(style, city_styles[style].graphic_alt);
  }

  if (city_styles[style].tiles_num == 0) {
      /* no alternate, use default */

    freelog(LOG_NORMAL,
	    "No tiles for alternate %s style, using default tiles",
            city_styles[style].graphic_alt);

    sprites.city.tile[style][0] = load_sprite("cd.city");
    sprites.city.tile[style][1] = load_sprite("cd.city_wall");
    sprites.city.tile[style][2] = load_sprite("cd.occupied");
    city_styles[style].tiles_num = 1;
    city_styles[style].tresh[0] = 0;
  }
}

/**********************************************************************
  alloc memory for city tiles sprites
***********************************************************************/
void tilespec_alloc_city_tiles(int count)
{
  int i;

  if (is_isometric)
    sprites.city.tile_wall = fc_calloc( count, sizeof(struct Sprite**) );
  sprites.city.tile = fc_calloc( count, sizeof(struct Sprite**) );

  for (i=0; i<count; i++) {
    if (is_isometric)
      sprites.city.tile_wall[i] = fc_calloc(MAX_CITY_TILES+2, sizeof(struct Sprite*));
    sprites.city.tile[i] = fc_calloc(MAX_CITY_TILES+2, sizeof(struct Sprite*));
  }
}

/**********************************************************************
  alloc memory for city tiles sprites
***********************************************************************/
void tilespec_free_city_tiles(int count)
{
  int i;

  for (i=0; i<count; i++) {
    if (is_isometric) {
      free(sprites.city.tile_wall[i]);
      sprites.city.tile_wall[i] = NULL;
    }
    free(sprites.city.tile[i]);
    sprites.city.tile[i] = NULL;
  }

  if (is_isometric) {
    free(sprites.city.tile_wall);
    sprites.city.tile_wall = NULL;
  }
  free(sprites.city.tile);
  sprites.city.tile = NULL;
}

/**********************************************************************
  Not sure which module to put this in...
  It used to be that each nation had a color, when there was
  fixed number of nations.  Now base on player number instead,
  since still limited to less than 14.  Could possibly improve
  to allow players to choose their preferred color etc.
  A hack added to avoid returning more that COLOR_STD_RACE13.
  But really there should be more colors available -- jk.
***********************************************************************/
enum color_std player_color(struct player *pplayer)
{
  return COLOR_STD_RACE0 +
    (pplayer->player_no %
     (COLOR_STD_RACE13 - COLOR_STD_RACE0 + 1));
}

/**********************************************************************
  Return color for overview map tile.
***********************************************************************/
enum color_std overview_tile_color(struct tile *ptile)
{
  enum color_std color;
  struct unit *punit;
  struct city *pcity;

  if (tile_get_known(ptile) == TILE_UNKNOWN) {
    color=COLOR_STD_BLACK;
  } else if((pcity=map_get_city(ptile))) {
    if(pcity->owner==game.player_idx)
      color=COLOR_STD_WHITE;
    else
      color=COLOR_STD_CYAN;
  } else if ((punit=find_visible_unit(ptile))) {
    if(punit->owner==game.player_idx)
      color=COLOR_STD_YELLOW;
    else
      color=COLOR_STD_RED;
  } else if (is_ocean(ptile->terrain)) {
    if (tile_get_known(ptile) == TILE_KNOWN_FOGGED && draw_fog_of_war) {
      color = COLOR_STD_RACE4;
    } else {
      color = COLOR_STD_OCEAN;
    }
  } else {
    if (tile_get_known(ptile) == TILE_KNOWN_FOGGED && draw_fog_of_war) {
      color = COLOR_STD_BACKGROUND;
    } else {
      color = COLOR_STD_GROUND;
    }
  }

  return color;
}

/**********************************************************************
  Set focus_unit_hidden (q.v.) variable to given value.
***********************************************************************/
void set_focus_unit_hidden_state(bool hide)
{
  focus_unit_hidden = hide;
}

/**********************************************************************
...
***********************************************************************/
struct unit *get_drawable_unit(struct tile *ptile, bool citymode)
{
  struct unit *punit = find_visible_unit(ptile);
  struct unit *pfocus = get_unit_in_focus();

  if (!punit)
    return NULL;

  if (citymode && punit->owner == game.player_idx)
    return NULL;

  if (punit != pfocus
      || !focus_unit_hidden
      || !same_pos(punit->tile, pfocus->tile))
    return punit;
  else
    return NULL;
}

static void unload_all_sprites(void )
{
  int i, entries = hash_num_entries(sprite_hash);

  for (i = 0; i < entries; i++) {
    const char *tag_name = hash_key_by_number(sprite_hash, i);
    struct small_sprite *ss = hash_lookup_data(sprite_hash, tag_name);

    while (ss->ref_count > 0) {
      unload_sprite(tag_name);
    }
  }
}

/**********************************************************************
...
***********************************************************************/
void tilespec_free_tiles(void)
{
  int i, entries = hash_num_entries(sprite_hash);

  freelog(LOG_DEBUG, "tilespec_free_tiles");

  unload_all_sprites();

  for (i = 0; i < entries; i++) {
    const char *key = hash_key_by_number(sprite_hash, 0);

    hash_delete_entry(sprite_hash, key);
    free((void *) key);
  }

  hash_free(sprite_hash);
  sprite_hash = NULL;

  small_sprite_list_iterate(small_sprites, ss) {
    small_sprite_list_unlink(&small_sprites, ss);
    if (ss->file) {
      free(ss->file);
    }
    assert(ss->sprite == NULL);
    free(ss);
  } small_sprite_list_iterate_end;

  specfile_list_iterate(specfiles, sf) {
    specfile_list_unlink(&specfiles, sf);
    free(sf->file_name);
    if (sf->big_sprite) {
      free_sprite(sf->big_sprite);
      sf->big_sprite = NULL;
    }
    free(sf);
  } specfile_list_iterate_end;

  if (num_tiles_explode_unit > 0) {
    free(sprites.explode.unit);
  }
}

/**************************************************************************
  Return a sprite for the given citizen.  The citizen's type is given,
  as well as their index (in the range [0..pcity->size)).  The
  citizen's city can be used to determine which sprite to use (a NULL
  value indicates there is no city; i.e., the sprite is just being
  used as a picture).
**************************************************************************/
struct Sprite *get_citizen_sprite(struct citizen_type type,
				  int citizen_index,
				  const struct city *pcity)
{
  struct citizen_graphic *graphic;

  if (type.type == CITIZEN_SPECIALIST) {
    graphic = &sprites.specialist[type.spec_type];
  } else {
    graphic = &sprites.citizen[type.type];
  }

  return graphic->sprite[citizen_index % graphic->count];
}

/**************************************************************************
  Loads the sprite. If the sprite is already loaded a reference
  counter is increased. Can return NULL if the sprite couldn't be
  loaded.
**************************************************************************/
struct Sprite *load_sprite(const char *tag_name)
{
  /* Lookup information about where the sprite is found. */
  struct small_sprite *ss = hash_lookup_data(sprite_hash, tag_name);

  freelog(LOG_DEBUG, "load_sprite(tag='%s')", tag_name);
  if (!ss) {
    return NULL;
  }

  assert(ss->ref_count >= 0);

  if (!ss->sprite) {
    /* If the sprite hasn't been loaded already, then load it. */
    assert(ss->ref_count == 0);
    if (ss->file) {
      ss->sprite = load_gfx_file(ss->file);
      if (!ss->sprite) {
	freelog(LOG_FATAL, _("Couldn't load gfx file %s for sprite %s"),
		ss->file, tag_name);
	exit(EXIT_FAILURE);
      }
    } else {
      int sf_w, sf_h;

      ensure_big_sprite(ss->sf);
      get_sprite_dimensions(ss->sf->big_sprite, &sf_w, &sf_h);
      if (ss->x < 0 || ss->x + ss->width > sf_w
	  || ss->y < 0 || ss->y + ss->height > sf_h) {
	freelog(LOG_ERROR,
		"Sprite '%s' in file '%s' isn't within the image!",
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
  Unloads the sprite. Decrease the reference counter. If the last
  reference is removed the sprite is freed.
**************************************************************************/
void unload_sprite(const char *tag_name)
{
  struct small_sprite *ss = hash_lookup_data(sprite_hash, tag_name);

  assert(ss);
  assert(ss->ref_count >= 1);
  assert(ss->sprite);

  ss->ref_count--;

  if (ss->ref_count == 0) {
    /* Nobody's using the sprite anymore, so we should free it.  We know
     * where to find it if we need it again. */
    freelog(LOG_DEBUG, "freeing sprite '%s'", tag_name);
    free_sprite(ss->sprite);
    ss->sprite = NULL;
  }
}

/**************************************************************************
  Return TRUE iff the specified sprite exists in the tileset (whether
  or not it is currently loaded).
**************************************************************************/
bool sprite_exists(const char *tag_name)
{
  /* Lookup information about where the sprite is found. */
  struct small_sprite *ss = hash_lookup_data(sprite_hash, tag_name);

  return (ss != NULL);
}

/**************************************************************************
  Frees any internal buffers which are created by load_sprite. Should
  be called after the last (for a given period of time) load_sprite
  call.  This saves a fair amount of memory, but it will take extra time
  the next time we start loading sprites again.
**************************************************************************/
void finish_loading_sprites(void)
{
  specfile_list_iterate(specfiles, sf) {
    if (sf->big_sprite) {
      free_sprite(sf->big_sprite);
      sf->big_sprite = NULL;
    }
  } specfile_list_iterate_end;
}
