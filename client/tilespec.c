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
#include <ctype.h>
#include <stdio.h>
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

#include "civclient.h" /* for get_client_state() */
#include "climap.h" /* for tile_get_known() */
#include "control.h" /* for fill_xxx */
#include "graphics_g.h"
#include "mapview_g.h" /* for update_map_canvas_visible */
#include "options.h" /* for fill_xxx */

#include "tilespec.h"

#define TILESPEC_SUFFIX ".tilespec"

char *main_intro_filename;
char *minimap_intro_filename;

struct named_sprites sprites;
char current_tile_set_name[512];

static const int DIR4_TO_DIR8[4] =
    { DIR8_NORTH, DIR8_SOUTH, DIR8_EAST, DIR8_WEST };

int NORMAL_TILE_WIDTH;
int NORMAL_TILE_HEIGHT;
int UNIT_TILE_WIDTH;
int UNIT_TILE_HEIGHT;
int SMALL_TILE_WIDTH;
int SMALL_TILE_HEIGHT;

bool is_isometric;

char *city_names_font;
char *city_productions_font_name;

bool flags_are_transparent = TRUE;

int num_tiles_explode_unit=0;

static bool is_mountainous = FALSE;
static int roadstyle;

static int num_spec_files = 0;
static char **spec_filenames;	/* full pathnames */

static struct hash_table *sprite_hash = NULL;
/*
   This hash table sprite_hash maps tilespec tags to tile Sprite pointers.
   This is kept permanently after setup, for doing lookups on ruleset
   information (including on reconnections etc).
*/

#define TILESPEC_CAPSTR "+tilespec2 duplicates_ok roadstyle"
/*
   Tilespec capabilities acceptable to this program:
   +tilespec2     -  basic format, required
   duplicates_ok  -  we can handle existence of duplicate tags
                     (lattermost tag which appears is used; tilesets which
		     have duplicates should specify "+duplicates_ok")
   roadstyle      -  Allow the use of tilespec.roadstyle to control which
                     style of road drawing to use.  Tilesets which rely on
                     this (those that have roadstyle != is_isometric ? 0 : 1)
                     should specify "+roadstyle".
*/

#define SPEC_CAPSTR "+spec2"
/*
   Individual spec file capabilities acceptable to this program:
   +spec2          -  basic format, required
*/


/*
  If focus_unit_hidden is true, then no units at
  the location of the foc unit are ever drawn.
*/
static bool focus_unit_hidden = FALSE;

/*
  If no_backdrop is true, then no color/flag is drawn behind the city/unit.
*/
static bool no_backdrop = FALSE;

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
   * it here, since this is the only place where we know it. */
  sz_strlcpy(current_tile_set_name, tileset_name);

  fname = fc_malloc(strlen(tileset_name) + strlen(TILESPEC_SUFFIX) + 1);
  sprintf(fname, "%s%s", tileset_name, TILESPEC_SUFFIX);
  
  dname = datafilename(fname);
  free(fname);

  if (dname) {
    return mystrdup(dname);
  }

  if (strcmp(tileset_name, tileset_default) == 0) {
    freelog(LOG_FATAL, _("No useable default tileset found, aborting!"));
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
static void check_tilespec_capabilities(struct section_file *file,
					const char *which,
					const char *us_capstr,
					const char *filename)
{
  char *file_capstr = secfile_lookup_str(file, "%s.options", which);
  
  if (!has_capabilities(us_capstr, file_capstr)) {
    freelog(LOG_FATAL, _("%s file appears incompatible:"), which);
    freelog(LOG_FATAL, _("file: \"%s\""), filename);
    freelog(LOG_FATAL, _("file options: %s"), file_capstr);
    freelog(LOG_FATAL, _("supported options: %s"), us_capstr);
    exit(EXIT_FAILURE);
  }
  if (!has_capabilities(file_capstr, us_capstr)) {
    freelog(LOG_FATAL, _("%s file claims required option(s)"
			 " which we don't support:"), which);
    freelog(LOG_FATAL, _("file: \"%s\""), filename);
    freelog(LOG_FATAL, _("file options: %s"), file_capstr);
    freelog(LOG_FATAL, _("supported options: %s"), us_capstr);
    exit(EXIT_FAILURE);
  }
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
  /* FIXME: free spec_filenames */
}

/**********************************************************************
  Read a new tilespec in from scratch.

  Unlike the initial reading code, which reads pieces one at a time,
  this gets rid of the old data and reads in the new all at once.

  It will also call the necessary functions to redraw the graphics.
***********************************************************************/
void tilespec_reread(const char *tileset_name)
{
  int id;
  int center_x, center_y;
  enum client_states state = get_client_state();

  freelog(LOG_NORMAL, "Loading tileset %s.", tileset_name);

  /* Step 0:  Record old data.
   *
   * We record the current mapcanvas center, etc.
   */
  get_center_tile_mapcanvas(&center_x, &center_y);

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
  tilespec_read_toplevel(tileset_name);
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
  for (id = 0; id < game.government_count; id++) {
    tilespec_setup_government(id);
  }
  for (id = 0; id < game.nation_count; id++) {
    tilespec_setup_nation_flag(id);
  }
  impr_type_iterate(imp_id) {
    tilespec_setup_impr_type(imp_id);
  } impr_type_iterate_end;
  for (id = 0; id < game.num_tech_types; id++) {
    if (tech_exists(id)) {
      tilespec_setup_tech_type(id);
    }
  }

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
  tileset_changed();
  center_tile_mapcanvas(center_x, center_y);
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

/**********************************************************************
  Finds and reads the toplevel tilespec file based on given name.
  Sets global variables, including tile sizes and full names for
  intro files.
***********************************************************************/
void tilespec_read_toplevel(const char *tileset_name)
{
  struct section_file the_file, *file = &the_file;
  char *fname, *c;
  int i;

  fname = tilespec_fullname(tileset_name);
  freelog(LOG_VERBOSE, "tilespec file is %s", fname);

  if (!section_file_load(file, fname)) {
    freelog(LOG_FATAL, _("Could not open \"%s\"."), fname);
    exit(EXIT_FAILURE);
  }
  check_tilespec_capabilities(file, "tilespec", TILESPEC_CAPSTR, fname);

  (void) section_file_lookup(file, "tilespec.name"); /* currently unused */

  is_isometric = secfile_lookup_bool_default(file, FALSE, "tilespec.is_isometric");
  if (is_isometric && !isometric_view_supported()) {
    freelog(LOG_ERROR, _("Client does not support isometric tilesets."
	    " Using default tileset instead."));
    assert(tileset_name != NULL);
    section_file_free(file);
    free(fname);
    tilespec_read_toplevel(NULL);
    return;
  }
  if (!is_isometric && !overhead_view_supported()) {
    freelog(LOG_ERROR, _("Client does not support overhead view tilesets."
	    " Using default tileset instead."));
    assert(tileset_name != NULL);
    section_file_free(file);
    free(fname);
    tilespec_read_toplevel(NULL);
    return;
  }

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

  is_mountainous = secfile_lookup_bool_default(file, FALSE,
					       "tilespec.is_mountainous");
  roadstyle = secfile_lookup_int_default(file, is_isometric ? 0 : 1,
					 "tilespec.roadstyle");

  c = secfile_lookup_str_default(file, "10x20", "tilespec.city_names_font");
  city_names_font = mystrdup(c);

  c =
      secfile_lookup_str_default(file, "8x16",
				 "tilespec.city_productions_font");
  city_productions_font_name = mystrdup(c);

  flags_are_transparent =
    secfile_lookup_bool(file, "tilespec.flags_are_transparent"); 

  c = secfile_lookup_str(file, "tilespec.main_intro_file");
  main_intro_filename = tilespec_gfx_filename(c);
  freelog(LOG_DEBUG, "intro file %s", main_intro_filename);
  
  c = secfile_lookup_str(file, "tilespec.minimap_intro_file");
  minimap_intro_filename = tilespec_gfx_filename(c);
  freelog(LOG_DEBUG, "radar file %s", minimap_intro_filename);

  spec_filenames = secfile_lookup_str_vec(file, &num_spec_files,
					  "tilespec.files");
  if (num_spec_files == 0) {
    freelog(LOG_FATAL, "No tile files specified in \"%s\"", fname);
    exit(EXIT_FAILURE);
  }
  
  for(i=0; i<num_spec_files; i++) {
    spec_filenames[i] = mystrdup(datafilename_required(spec_filenames[i]));
    freelog(LOG_DEBUG, "spec file %s", spec_filenames[i]);
  }

  section_file_check_unused(file, fname);
  
  section_file_free(file);
  freelog(LOG_VERBOSE, "finished reading %s", fname);
  free(fname);
}

/**********************************************************************
  Load one specfile and specified xpm file; splits xpm into tiles,
  and save sprites in sprite_hash.
***********************************************************************/
static void tilespec_load_one(const char *spec_filename)
{
  struct section_file the_file, *file = &the_file;
  struct Sprite *big_sprite = NULL, *small_sprite;
  const char *gfx_filename, *gfx_current_fileext, **gfx_fileexts;
  char **gridnames, **tags;
  int num_grids, num_tags;
  int i, j, k;
  int x_top_left, y_top_left, dx, dy;
  int row, column;
  int x1, y1;

  freelog(LOG_DEBUG, "loading spec %s", spec_filename);
  if (!section_file_load(file, spec_filename)) {
    freelog(LOG_FATAL, _("Could not open \"%s\"."), spec_filename);
    exit(EXIT_FAILURE);
  }
  check_tilespec_capabilities(file, "spec", SPEC_CAPSTR, spec_filename);

  (void) section_file_lookup(file, "info.artists"); /* currently unused */

  gfx_fileexts = gfx_fileextensions();
  gfx_filename = secfile_lookup_str(file, "file.gfx");

  while((!big_sprite) && (gfx_current_fileext = *gfx_fileexts++))
  {
    char *full_name,*real_full_name;
    full_name = fc_malloc(strlen(gfx_filename)+strlen(gfx_current_fileext)+2);
    sprintf(full_name,"%s.%s",gfx_filename,gfx_current_fileext);

    if((real_full_name = datafilename(full_name)))
    {
      freelog(LOG_DEBUG, "trying to load gfx file %s", real_full_name);
      if(!(big_sprite = load_gfxfile(real_full_name)))
      {
        freelog(LOG_VERBOSE, "loading the gfx file %s failed", real_full_name);
      }
    }
    free(full_name);
  }

  if(!big_sprite) {
    freelog(LOG_FATAL, _("Couldn't load gfx file for the spec file %s"),
	    spec_filename);
    exit(EXIT_FAILURE);
  }


  gridnames = secfile_get_secnames_prefix(file, "grid_", &num_grids);
  if (num_grids==0) {
    freelog(LOG_FATAL, "spec %s has no grid_* sections", spec_filename);
    exit(EXIT_FAILURE);
  }

  for(i=0; i<num_grids; i++) {
    bool is_pixel_border =
      secfile_lookup_bool_default(file, FALSE, "%s.is_pixel_border", gridnames[i]);
    x_top_left = secfile_lookup_int(file, "%s.x_top_left", gridnames[i]);
    y_top_left = secfile_lookup_int(file, "%s.y_top_left", gridnames[i]);
    dx = secfile_lookup_int(file, "%s.dx", gridnames[i]);
    dy = secfile_lookup_int(file, "%s.dy", gridnames[i]);

    j = -1;
    while(section_file_lookup(file, "%s.tiles%d.tag", gridnames[i], ++j)) {
      row = secfile_lookup_int(file, "%s.tiles%d.row", gridnames[i], j);
      column = secfile_lookup_int(file, "%s.tiles%d.column", gridnames[i], j);
      tags = secfile_lookup_str_vec(file, &num_tags, "%s.tiles%d.tag",
				    gridnames[i], j);

      /* there must be at least 1 because of the while(): */
      assert(num_tags>0);

      x1 = x_top_left + column * dx + (is_pixel_border ? column : 0);
      y1 = y_top_left + row * dy + (is_pixel_border ? row : 0);
      small_sprite = crop_sprite(big_sprite, x1, y1, dx, dy);

      for(k=0; k<num_tags; k++) {
	/* don't free old sprite, since could be multiple tags
	   pointing to it */
	(void) hash_replace(sprite_hash, mystrdup(tags[k]), small_sprite);
      }
      free(tags);
      tags = NULL;
    }
  }
  free(gridnames);
  gridnames = NULL;

  free_sprite(big_sprite);
  
  section_file_check_unused(file, spec_filename);
  section_file_free(file);
  freelog(LOG_DEBUG, "finished %s", spec_filename);
}

/**********************************************************************
  Returns a text name for the citizen, as used in the tileset.
***********************************************************************/
static const char *get_citizen_name(enum citizen_type citizen)
{
  /* These strings are used in reading the tileset.  Do not
   * translate. */
  switch (citizen) {
  case CITIZEN_ELVIS:
    return "entertainer";
  case CITIZEN_SCIENTIST:
    return "scientist";
  case CITIZEN_TAXMAN:
    return "tax_collector";
  case CITIZEN_HAPPY:
    return "happy";
  case CITIZEN_CONTENT:
    return "content";
  case CITIZEN_UNHAPPY:
    return "unhappy";
  case CITIZEN_ANGRY:
    return "angry";
  default:
    die("unknown citizen type %d", (int) citizen);
  }
  return NULL;
}

/**********************************************************************
  Return string n0s0e0w0 such that INDEX_NSEW(n,s,e,w) = idx
  The returned string is pointer to static memory.
***********************************************************************/
static char *nsew_str(int idx)
{
  static char c[9]; /* strlen("n0s0e0w0") + 1 == 9 */

  sprintf(c, "n%ds%de%dw%d", BOOL_VAL(idx&BIT_NORTH), BOOL_VAL(idx&BIT_SOUTH),
     	                     BOOL_VAL(idx&BIT_EAST),  BOOL_VAL(idx&BIT_WEST));
  return c;
}
     
/* Not very safe, but convenient: */
#define SET_SPRITE(field, tag) do { \
       sprites.field = hash_lookup_data(sprite_hash, tag);\
       assert(sprites.field != NULL);\
    } while(FALSE)

/* Sets sprites.field to tag or (if tag isn't available) to alt */
#define SET_SPRITE_ALT(field, tag, alt) do {                   \
       sprites.field = hash_lookup_data(sprite_hash, tag);     \
       if (!sprites.field) {                                   \
           sprites.field = hash_lookup_data(sprite_hash, alt); \
       }                                                       \
       assert(sprites.field != NULL);                          \
    } while(FALSE)

/* Sets sprites.field to tag, or NULL if not available */
#define SET_SPRITE_OPT(field, tag) \
  sprites.field = hash_lookup_data(sprite_hash, tag)

/**********************************************************************
  Initialize 'sprites' structure based on hardwired tags which
  freeciv always requires. 
***********************************************************************/
static void tilespec_lookup_sprite_tags(void)
{
  char buffer[512];
  const char dir_char[] = "nsew";
  int i, j;
  
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
    SET_SPRITE(coast_color, "t.coast_color");
  }

  /* Load the citizen sprite graphics. */
  for (i = 0; i < NUM_TILES_CITIZEN; i++) {
    my_snprintf(buffer, sizeof(buffer), "citizen.%s", get_citizen_name(i));
    sprites.citizen[i].sprite[0] = hash_lookup_data(sprite_hash, buffer);
    if (sprites.citizen[i].sprite[0]) {
      /*
       * If this form exists, use it as the only sprite.  This allows
       * backwards compatability with tilesets that use e.g.,
       * citizen.entertainer.
       */
      sprites.citizen[i].count = 1;
      continue;
    }

    for (j = 0; j < NUM_TILES_CITIZEN; j++) {
      my_snprintf(buffer, sizeof(buffer), "citizen.%s_%d",
		  get_citizen_name(i), j);
      sprites.citizen[i].sprite[j] = hash_lookup_data(sprite_hash, buffer);
      if (!sprites.citizen[i].sprite[j]) {
	break;
      }
    }
    sprites.citizen[i].count = j;
    assert(j > 0);
  }

  SET_SPRITE(spaceship.solar_panels, "spaceship.solar_panels");
  SET_SPRITE(spaceship.life_support, "spaceship.life_support");
  SET_SPRITE(spaceship.habitation,   "spaceship.habitation");
  SET_SPRITE(spaceship.structural,   "spaceship.structural");
  SET_SPRITE(spaceship.fuel,         "spaceship.fuel");
  SET_SPRITE(spaceship.propulsion,   "spaceship.propulsion");

  /* Isolated road graphics are used by roadstyle 0 and 1. */
  SET_SPRITE(road.isolated, "r.road_isolated");
  SET_SPRITE(rail.isolated, "r.rail_isolated");
  
  if (roadstyle == 0) {
    /* Roadstyle 0 has just 8 additional sprites for both road and rail:
     * one for the road/rail going off in each direction. */
    for (i = 0; i < 8; i++) {
      const char *s = dir_get_name(i);
      char dir_name[3];

      assert(strlen(s) == 1 || strlen(s) == 2);
      dir_name[0] = my_tolower(s[0]);
      dir_name[1] = my_tolower(s[1]);
      dir_name[2] = my_tolower(s[2]);

      my_snprintf(buffer, sizeof(buffer), "r.road_%s", dir_name);
      SET_SPRITE(road.dir[i], buffer);
      my_snprintf(buffer, sizeof(buffer), "r.rail_%s", dir_name);
      SET_SPRITE(rail.dir[i], buffer);
    }
  } else {
    /* Roadstyle 1 has 32 addidional sprites for both road and rail:
     * 16 each for cardinal and diagonal directions.  Each set
     * of 16 provides a NSEW-indexed sprite to provide connectors for
     * all rails in the cardinal/diagonal directions.  The 0 entry is
     * unused (the "isolated" sprite is used instead). */
    for(i=1; i<NUM_DIRECTION_NSEW; i++) {
      my_snprintf(buffer, sizeof(buffer), "r.c_road_%s", nsew_str(i));
      SET_SPRITE(road.cardinal[i], buffer);

      my_snprintf(buffer, sizeof(buffer), "r.d_road_%s", nsew_str(i));
      SET_SPRITE(road.diagonal[i], buffer);

      my_snprintf(buffer, sizeof(buffer), "r.c_rail_%s", nsew_str(i));
      SET_SPRITE(rail.cardinal[i], buffer);

      my_snprintf(buffer, sizeof(buffer), "r.d_rail_%s", nsew_str(i));
      SET_SPRITE(rail.diagonal[i], buffer);
    }
  }

  /* Corner road/rail graphics are used by roadstyle 0 and 1. */
  SET_SPRITE_OPT(rail.corner[DIR8_NORTHWEST], "r.c_rail_nw");
  SET_SPRITE_OPT(rail.corner[DIR8_NORTHEAST], "r.c_rail_ne");
  SET_SPRITE_OPT(rail.corner[DIR8_SOUTHWEST], "r.c_rail_sw");
  SET_SPRITE_OPT(rail.corner[DIR8_SOUTHEAST], "r.c_rail_se");

  SET_SPRITE_OPT(road.corner[DIR8_NORTHWEST], "r.c_road_nw");
  SET_SPRITE_OPT(road.corner[DIR8_NORTHEAST], "r.c_road_ne");
  SET_SPRITE_OPT(road.corner[DIR8_SOUTHWEST], "r.c_road_sw");
  SET_SPRITE_OPT(road.corner[DIR8_SOUTHEAST], "r.c_road_se");

  if (is_isometric) {
    SET_SPRITE(explode.iso_nuke, "explode.iso_nuke");
  } else {
    for(i=0; i<3; i++) {
      for(j=0; j<3; j++) {
	my_snprintf(buffer, sizeof(buffer), "explode.nuke_%d%d", i, j);
	SET_SPRITE(explode.nuke[i][j], buffer);
      }
    }
  }

  num_tiles_explode_unit = 0;
  do {
    my_snprintf(buffer, sizeof(buffer), "explode.unit_%d",
		num_tiles_explode_unit++);
  } while (hash_key_exists(sprite_hash, buffer));
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
  SET_SPRITE(unit.transform,    "unit.transform");
  SET_SPRITE(unit.connect,      "unit.connect");
  SET_SPRITE(unit.patrol,       "unit.patrol");

  for(i=0; i<NUM_TILES_HP_BAR; i++) {
    my_snprintf(buffer, sizeof(buffer), "unit.hp_%d", i*10);
    SET_SPRITE(unit.hp_bar[i], buffer);
  }

  SET_SPRITE(city.disorder, "city.disorder");

  for(i=0; i<NUM_TILES_DIGITS; i++) {
    my_snprintf(buffer, sizeof(buffer), "city.size_%d", i);
    SET_SPRITE(city.size[i], buffer);
    if(i!=0) {
      my_snprintf(buffer, sizeof(buffer), "city.size_%d", i*10);
      SET_SPRITE(city.size_tens[i], buffer);
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
  SET_SPRITE(upkeep.shield, "upkeep.shield");
  
  SET_SPRITE(user.attention, "user.attention");

  SET_SPRITE(tx.fallout,    "tx.fallout");
  SET_SPRITE(tx.farmland,   "tx.farmland");
  SET_SPRITE(tx.irrigation, "tx.irrigation");
  SET_SPRITE(tx.mine,       "tx.mine");
  SET_SPRITE_ALT(tx.oil_mine, "tx.oil_mine", "tx.mine");
  SET_SPRITE(tx.pollution,  "tx.pollution");
  SET_SPRITE(tx.village,    "tx.village");
  SET_SPRITE(tx.fortress,   "tx.fortress");
  SET_SPRITE_ALT(tx.fortress_back, "tx.fortress_back", "tx.fortress");
  SET_SPRITE(tx.airbase,    "tx.airbase");
  SET_SPRITE(tx.fog,        "tx.fog");

  for(i=0; i<NUM_DIRECTION_NSEW; i++) {
    my_snprintf(buffer, sizeof(buffer), "tx.s_river_%s", nsew_str(i));
    SET_SPRITE(tx.spec_river[i], buffer);
  }

  if (is_isometric) {
    for(i=0; i<NUM_DIRECTION_NSEW; i++) {
      my_snprintf(buffer, sizeof(buffer), "tx.s_forest_%s", nsew_str(i));
      SET_SPRITE(tx.spec_forest[i], buffer);
    }

    for(i=0; i<NUM_DIRECTION_NSEW; i++) {
      my_snprintf(buffer, sizeof(buffer), "tx.s_mountain_%s", nsew_str(i));
      SET_SPRITE(tx.spec_mountain[i], buffer);
    }

    for(i=0; i<NUM_DIRECTION_NSEW; i++) {
      my_snprintf(buffer, sizeof(buffer), "tx.s_hill_%s", nsew_str(i));
      SET_SPRITE(tx.spec_hill[i], buffer);
    }

    for (i=0; i<4; i++) {
      for (j=0; j<8; j++) {
	char *dir2 = "udlr";
	my_snprintf(buffer, sizeof(buffer), "tx.coast_cape_%c%d", dir2[i], j);
	SET_SPRITE(tx.coast_cape_iso[j][i], buffer);
      }
    }
  } else {
    for(i=1; i<NUM_DIRECTION_NSEW; i++) {
      my_snprintf(buffer, sizeof(buffer), "tx.darkness_%s", nsew_str(i));
      SET_SPRITE(tx.darkness[i], buffer);
    }

    for(i=1; i<NUM_DIRECTION_NSEW; i++) {
      my_snprintf(buffer, sizeof(buffer), "tx.coast_cape_%s", nsew_str(i));
      SET_SPRITE(tx.coast_cape[i], buffer);
    }

    for(i=0; i<2; i++) {
      for(j=0; j<3; j++) {
	my_snprintf(buffer, sizeof(buffer), "tx.denmark_%d%d", i, j);
	SET_SPRITE(tx.denmark[i][j], buffer);
      }
    }
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
  int i;
  
  assert(num_spec_files>0);
  sprite_hash = hash_new(hash_fval_string, hash_fcmp_string);

  for(i=0; i<num_spec_files; i++) {
    tilespec_load_one(spec_filenames[i]);
  }

  tilespec_lookup_sprite_tags();
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
  int loglevel = required ? LOG_NORMAL : LOG_DEBUG;
  
  /* (should get sprite_hash before connection) */
  if (!sprite_hash) {
    die("attempt to lookup for %s %s before sprite_hash setup", what, name);
  }

  sp = hash_lookup_data(sprite_hash, tag);
  if (sp) return sp;

  sp = hash_lookup_data(sprite_hash, alt);
  if (sp) {
    freelog(loglevel, "Using alternate graphic %s (instead of %s) for %s %s",
	    alt, tag, what, name);
    return sp;
  }
  
  freelog(loglevel, "Don't have graphics tags %s or %s for %s %s",
	  tag, alt, what, name);
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
					improvement_exists(id), "impr_type",
					pimpr->name);

  /* should maybe do something if NULL, eg generic default? */
}

/**********************************************************************
  Set tech_type sprite value; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tilespec_setup_tech_type(int id)
{
  advances[id].sprite = lookup_sprite_tag_alt(advances[id].graphic_str,
					      advances[id].graphic_alt,
					      tech_exists(id), "tech_type",
					      advances[id].name);

  /* should maybe do something if NULL, eg generic default? */
}

/**********************************************************************
  Set tile_type sprite values; should only happen after
  tilespec_load_tiles().
***********************************************************************/
void tilespec_setup_tile_type(int id)
{
  struct tile_type *tt = get_tile_type(id);
  char buffer1[MAX_LEN_NAME+20];
  char buffer2[MAX_LEN_NAME+20];
  char *nsew;
  int i;
  
  if(tt->terrain_name[0] == '\0') {
    for(i=0; i<NUM_DIRECTION_NSEW; i++) {
      tt->sprite[i] = NULL;
    }
    for(i=0; i<2; i++) {
      tt->special[i].sprite = NULL;
    }
    return;
  }

  if (is_isometric) {
    my_snprintf(buffer1, sizeof(buffer1), "%s1", tt->graphic_str);
    if (id != T_RIVER) {
      tt->sprite[0] = lookup_sprite_tag_alt(buffer1, NULL, TRUE, "tile_type",
					    tt->terrain_name);
    } else {
      tt->sprite[0] = NULL;
    }
  } else {
    for(i=0; i<NUM_DIRECTION_NSEW; i++) {
      nsew = nsew_str(i);
      my_snprintf(buffer1, sizeof(buffer1), "%s_%s", tt->graphic_str, nsew);
      my_snprintf(buffer2, sizeof(buffer2), "%s_%s", tt->graphic_alt, nsew);

      tt->sprite[i] = lookup_sprite_tag_alt(buffer1, buffer2, TRUE, "tile_type",
					    tt->terrain_name);

      assert(tt->sprite[i] != NULL);
      /* should probably do something if NULL, eg generic default? */
    }
  }

  for (i=0; i<2; i++) {
    char *name = (i != 0) ? tt->special_2_name : tt->special_1_name;
    if (name[0] != '\0') {
      tt->special[i].sprite
	= lookup_sprite_tag_alt(tt->special[i].graphic_str,
				tt->special[i].graphic_alt,
				name[0] != '\0', "tile_type special", name);
      assert(tt->special[i].sprite != NULL);
    } else {
      tt->special[i].sprite = NULL;
    }
    /* should probably do something if NULL, eg generic default? */
  }
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
  struct nation_type *this_nation = get_nation_by_idx(id);

  this_nation->flag_sprite = lookup_sprite_tag_alt(this_nation->flag_graphic_str, 
					    this_nation->flag_graphic_alt,
					    TRUE, "nation", this_nation->name);

  /* should probably do something if NULL, eg generic default? */
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

/**********************************************************************
  Fill in the sprite array for the city
***********************************************************************/
static int fill_city_sprite_array(struct Sprite **sprs, struct city *pcity,
				  int *solid_bg)
{
  struct Sprite **save_sprs=sprs;

  *solid_bg = 0;
  if (!no_backdrop) {
    if(!solid_color_behind_units) {
      /* will be the first sprite if flags_are_transparent == FALSE */
      *sprs++ = get_city_nation_flag_sprite(pcity);
    } else {
      *solid_bg = 1;
    }
  }

  if (pcity->occupied) {
    *sprs++ = get_city_occupied_sprite(pcity);
  }

  *sprs++ = get_city_sprite(pcity);

  if (city_got_citywalls(pcity))
    *sprs++ = get_city_wall_sprite(pcity);

  if(map_has_special(pcity->x, pcity->y, S_POLLUTION))
    *sprs++ = sprites.tx.pollution;
  if(map_has_special(pcity->x, pcity->y, S_FALLOUT))
    *sprs++ = sprites.tx.fallout;

  if(city_unhappy(pcity))
    *sprs++ = sprites.city.disorder;

  if(tile_get_known(pcity->x, pcity->y) == TILE_KNOWN_FOGGED && draw_fog_of_war)
    *sprs++ = sprites.tx.fog;

  /* Put the size sprites last, so that they are not obscured
   * (and because they can be hard to read if fogged).
   */
  if (pcity->size>=10)
    *sprs++ = sprites.city.size_tens[pcity->size/10];

  *sprs++ = sprites.city.size[pcity->size%10];

  return sprs - save_sprs;
}

/**********************************************************************
  Fill in the sprite array for the city
  (small because things have to be done later in isometric view.)
***********************************************************************/
int fill_city_sprite_array_iso(struct Sprite **sprs, struct city *pcity)
{
  struct Sprite **save_sprs=sprs;

  if (!no_backdrop) {
    *sprs++ = get_city_nation_flag_sprite(pcity);
  }

  if (pcity->occupied) {
    *sprs++ = get_city_occupied_sprite(pcity);
  }

  *sprs++ = get_city_sprite(pcity);

  if (city_unhappy(pcity))
    *sprs++ = sprites.city.disorder;

  return sprs - save_sprs;
}

/**************************************************************************
  Assemble some data that is used in building the tile sprite arrays.
    (map_x, map_y) : the (normalized) map position
  The values we fill in:
    ttype          : the terrain type of the tile
    tspecial       : all specials the tile has
    ttype_near     : terrain types of all adjacent terrain
    tspecial_near  : specials of all adjacent terrain
**************************************************************************/
static void build_tile_data(int map_x, int map_y,
			    enum tile_terrain_type *ttype,
			    enum tile_special_type *tspecial,
			    enum tile_terrain_type *ttype_near,
			    enum tile_special_type *tspecial_near)
{
  enum direction8 dir;

  *tspecial = map_get_special(map_x, map_y);
  *ttype = map_get_terrain(map_x, map_y);

  /* In iso view a river is drawn as an overlay on top of an underlying
   * grassland terrain. */
  if (is_isometric && *ttype == T_RIVER) {
    *ttype = T_GRASSLAND;
    *tspecial |= S_RIVER;
  }

  /* Loop over all adjacent tiles.  We should have an iterator for this. */
  for (dir = 0; dir < 8; dir++) {
    int x1, y1;

    if (MAPSTEP(x1, y1, map_x, map_y, dir)) {
      tspecial_near[dir] = map_get_special(x1, y1);
      ttype_near[dir] = map_get_terrain(x1, y1);

      /* hacking away the river here... */
      if (is_isometric && ttype_near[dir] == T_RIVER) {
	tspecial_near[dir] |= S_RIVER;
	ttype_near[dir] = T_GRASSLAND;
      }
    } else {
      /* We draw the edges of the map as if the same terrain just
       * continued off the edge of the map. */
      tspecial_near[dir] = S_NO_SPECIAL;
      ttype_near[dir] = *ttype;
    }
  }
}

/**********************************************************************
  Fill in the sprite array for the unit
***********************************************************************/
int fill_unit_sprite_array(struct Sprite **sprs, struct unit *punit,
			   int *solid_bg)
{
  struct Sprite **save_sprs=sprs;
  int ihp;
  *solid_bg = 0;

  if (is_isometric) {
    if (!no_backdrop) {
      *sprs++ = get_unit_nation_flag_sprite(punit);
    }
  } else {
    if (!no_backdrop) {
      if (!solid_color_behind_units) {
	/* will be the first sprite if flags_are_transparent == FALSE */
	*sprs++ = get_unit_nation_flag_sprite(punit);
      } else {
	*solid_bg = 1;
      }
    }
  }

  *sprs++ = unit_type(punit)->sprite;

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

    *sprs++ = s;
  }

  if (punit->ai.control && punit->activity != ACTIVITY_EXPLORE) {
    if (is_military_unit(punit)) {
      *sprs++ = sprites.unit.auto_attack;
    } else {
      *sprs++ = sprites.unit.auto_settler;
    }
  }

  if (punit->connecting) {
    *sprs++ = sprites.unit.connect;
  }

  if (punit->activity == ACTIVITY_PATROL) {
    *sprs++ = sprites.unit.patrol;
  }

  ihp = ((NUM_TILES_HP_BAR-1)*punit->hp) / unit_type(punit)->hp;
  ihp = CLIP(0, ihp, NUM_TILES_HP_BAR-1);
  *sprs++ = sprites.unit.hp_bar[ihp];

  return sprs - save_sprs;
}

/**********************************************************************
Only used for isometric view.
***********************************************************************/
static struct Sprite *get_dither(int ttype, int ttype_other)
{
  if (ttype_other == T_UNKNOWN)
    return sprites.black_tile;

  if (is_ocean(ttype) || ttype == T_JUNGLE) {
    return NULL;
  }

  if (is_ocean(ttype_other)) {
    return sprites.coast_color;
  }

  if (ttype_other != T_UNKNOWN && ttype_other != T_LAST)
    return get_tile_type(ttype_other)->sprite[0];
  else
    return NULL;
}

/**********************************************************************
  Return TRUE iff the two mountainous terrain types should be blended.

  If is_mountainous set, the tileset will be "mountainous" and consider
  adjacent hills and mountains interchangable.  If it's unset then
  hills will only blend with hills and mountains only with mountains.
***********************************************************************/
static bool can_blend_hills_and_mountains(enum tile_terrain_type ttype,
				  enum tile_terrain_type ttype_adjc)
{
  assert(ttype == T_HILLS || ttype == T_MOUNTAINS);
  if (is_mountainous) {
    return ttype_adjc == T_HILLS || ttype_adjc == T_MOUNTAINS;
  } else {
    return ttype_adjc == ttype;
  }
}

/**************************************************************************
  Add any corner road sprites to the sprite array.
**************************************************************************/
static int fill_road_corner_sprites(struct Sprite **sprs,
				    bool road, bool *road_near,
				    bool rail, bool *rail_near)
{
  struct Sprite **saved_sprs = sprs;

  assert(draw_roads_rails);
  if (!(draw_diagonal_roads || is_isometric)) {
    /* We're not supposed to draw roads/rails here. */
    /* HACK: draw_diagonal_roads is not checked in iso view. */
    return 0;
  }

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
  if (sprites.road.corner[DIR8_NORTHEAST]
      && (road_near[DIR8_NORTH] && road_near[DIR8_EAST]
	  && !(rail_near[DIR8_NORTH] && rail_near[DIR8_EAST]))
      && !(road && road_near[DIR8_NORTHEAST]
	   && !(rail && rail_near[DIR8_NORTHEAST]))) {
    *sprs++ = sprites.road.corner[DIR8_NORTHEAST];
  }
  if (sprites.road.corner[DIR8_NORTHWEST]
      && (road_near[DIR8_NORTH] && road_near[DIR8_WEST]
	  && !(rail_near[DIR8_NORTH] && rail_near[DIR8_WEST]))
      && !(road && road_near[DIR8_NORTHWEST]
	   && !(rail && rail_near[DIR8_NORTHWEST]))) {
    *sprs++ = sprites.road.corner[DIR8_NORTHWEST];
  }
  if (sprites.road.corner[DIR8_SOUTHEAST]
      && (road_near[DIR8_SOUTH] && road_near[DIR8_EAST]
	  && !(rail_near[DIR8_SOUTH] && rail_near[DIR8_EAST]))
      && !(road && road_near[DIR8_SOUTHEAST]
	   && !(rail && rail_near[DIR8_SOUTHEAST]))) {
    *sprs++ = sprites.road.corner[DIR8_SOUTHEAST];
  }
  if (sprites.road.corner[DIR8_SOUTHWEST]
      && (road_near[DIR8_SOUTH] && road_near[DIR8_WEST]
	  && !(rail_near[DIR8_SOUTH] && rail_near[DIR8_WEST]))
      && !(road && road_near[DIR8_SOUTHWEST]
	   && !(rail && rail_near[DIR8_SOUTHWEST]))) {
    *sprs++ = sprites.road.corner[DIR8_SOUTHWEST];
  }

  return sprs - saved_sprs;
}

/**************************************************************************
  Add any corner rail sprites to the sprite array.
**************************************************************************/
static int fill_rail_corner_sprites(struct Sprite **sprs,
				    bool rail, bool *rail_near)
{
  struct Sprite **saved_sprs = sprs;

  assert(draw_roads_rails);
  if (!(draw_diagonal_roads || is_isometric)) {
    /* We're not supposed to draw roads/rails here. */
    /* HACK: draw_diagonal_roads is not checked in iso view. */
    return 0;
  }

  /* Rails going diagonally adjacent to this tile need to be
   * partly drawn on this tile. */

  if (sprites.rail.corner[DIR8_NORTHEAST]
      && rail_near[DIR8_NORTH] && rail_near[DIR8_EAST]
      && !(rail && rail_near[DIR8_NORTHEAST])) {
    *sprs++ = sprites.rail.corner[DIR8_NORTHEAST];
  }
  if (sprites.rail.corner[DIR8_NORTHWEST]
      && rail_near[DIR8_NORTH] && rail_near[DIR8_WEST]
      && !(rail && rail_near[DIR8_NORTHWEST])) {
    *sprs++ = sprites.rail.corner[DIR8_NORTHWEST];
  }
  if (sprites.rail.corner[DIR8_SOUTHEAST]
      && rail_near[DIR8_SOUTH] && rail_near[DIR8_EAST]
      && !(rail && rail_near[DIR8_SOUTHEAST])) {
    *sprs++ = sprites.rail.corner[DIR8_SOUTHEAST];
  }
  if (sprites.rail.corner[DIR8_SOUTHWEST]
      && rail_near[DIR8_SOUTH] && rail_near[DIR8_WEST]
      && !(rail && rail_near[DIR8_SOUTHWEST])) {
    *sprs++ = sprites.rail.corner[DIR8_SOUTHWEST];
  }

  return sprs - saved_sprs;
}

/**************************************************************************
  Fill all road and rail sprites into the sprite array.
**************************************************************************/
static int fill_road_rail_sprite_array(struct Sprite **sprs,
				       enum tile_special_type tspecial,
				       enum tile_special_type *tspecial_near,
				       struct city *pcity)
{
  struct Sprite **saved_sprs = sprs;
  bool road, road_near[8], rail, rail_near[8];
  enum direction8 dir;

  if (!draw_roads_rails) {
    /* Don't draw anything. */
    return 0;
  }

  /* Fill some data arrays. */
  road = contains_special(tspecial, S_ROAD);
  rail = contains_special(tspecial, S_RAILROAD);
  for (dir = 0; dir < 8; dir++) {
    road_near[dir] = contains_special(tspecial_near[dir], S_ROAD);
    rail_near[dir] = contains_special(tspecial_near[dir], S_RAILROAD);
  }

  /* Draw road corners underneath rails. */
  sprs += fill_road_corner_sprites(sprs, road, road_near, rail, rail_near);

  if (roadstyle == 0) {
    /* With roadstyle 0, we simply draw one road/rail for every connection.
     * This means we only need a few sprites, but a lot of drawing is
     * necessary and it generally doesn't look very good. */

    /* TODO: check draw_diagonal_roads */

    /* Draw roads underneath rails. */
    if (road) {
      bool found = FALSE;

      for (dir = 0; dir < 8; dir++) {
	if (road_near[dir]) {
	  found = TRUE;
	  /* Only draw a road if there's no rail there. */
	  if (!(rail && rail_near[dir])) {
	    *sprs++ = sprites.road.dir[dir];
	  }
	}
      }

      if (!found && !pcity && !rail) {
	*sprs++ = sprites.road.isolated;
      }
    }

    /* Draw rails over roads. */
    if (rail) {
      bool found = FALSE;

      for (dir = 0; dir < 8; dir++) {
	if (rail_near[dir]) {
	  *sprs++ = sprites.rail.dir[dir];
	  found = TRUE;
	}
      }

      if (!found && !pcity) {
	*sprs++ = sprites.rail.isolated;
      }
    }
  } else {
    /* With roadstyle 1, we draw one sprite for cardinal road connections,
     * one sprite for diagonal road connections, and the same for rail.
     * This means we need about 4x more sprites than in style 0, but up to
     * 4x less drawing is needed.  The drawing quality may also be
     *  improved. */

    /* 'card' => cardinal; 'semi' => diagonal.
     * 'count => number of connections.
     * 'tileno' => index of sprite to use. */
    int rail_card_count, rail_semi_count, road_card_count, road_semi_count;
    int rail_card_tileno = 0, rail_semi_tileno = 0;
    int road_card_tileno = 0, road_semi_tileno = 0;

    if (road || rail) {
      bool n, s, e, w;

      /* Find cardinal rail connections. */
      n = rail_near[DIR8_NORTH];
      s = rail_near[DIR8_SOUTH];
      e = rail_near[DIR8_EAST];
      w = rail_near[DIR8_WEST];
      rail_card_count = !!n + !!s + !!e + !!w;
      rail_card_tileno = INDEX_NSEW(n, s, e, w);

      /* Find cardinal road connections. */
      n = road_near[DIR8_NORTH];
      s = road_near[DIR8_SOUTH];
      e = road_near[DIR8_EAST];
      w = road_near[DIR8_WEST];
      road_card_count = !!n + !!s + !!e + !!w;
      road_card_tileno = INDEX_NSEW(n, s, e, w);

      /* Find diagonal rail connections. */
      n = rail_near[DIR8_NORTHEAST];
      s = rail_near[DIR8_SOUTHWEST];
      e = rail_near[DIR8_SOUTHEAST];
      w = rail_near[DIR8_NORTHWEST];
      rail_semi_count = !!n + !!s + !!e + !!w;
      rail_semi_tileno = INDEX_NSEW(n, s, e, w);

      /* Find diagonal road connections. */
      n = road_near[DIR8_NORTHEAST];
      s = road_near[DIR8_SOUTHWEST];
      e = road_near[DIR8_SOUTHEAST];
      w = road_near[DIR8_NORTHWEST];
      road_semi_count = !!n + !!s + !!e + !!w;
      road_semi_tileno = INDEX_NSEW(n, s, e, w);

      if (rail) {
	/* Don't draw a road if a rail is present. */
	road_card_tileno &= ~rail_card_tileno;
	road_semi_tileno &= ~rail_semi_tileno;
      } else if (road) {
	/* Don't draw a rail if there isn't one present. */
	rail_card_tileno = 0;
	rail_semi_tileno = 0;
      }

      /* First draw roads under rails.  We draw the sprite with fewer
       * connections first - it is not clear why. */
      if (road_semi_count > road_card_count) {
	if (road_card_tileno != 0) {
	  *sprs++ = sprites.road.cardinal[road_card_tileno];
	}
	if (road_semi_tileno != 0 && draw_diagonal_roads) {
	  *sprs++ = sprites.road.diagonal[road_semi_tileno];
	}
      } else {
	if (road_semi_tileno != 0 && draw_diagonal_roads) {
	  *sprs++ = sprites.road.diagonal[road_semi_tileno];
	}
	if (road_card_tileno != 0) {
	  *sprs++ = sprites.road.cardinal[road_card_tileno];
	}
      }

      /* Then draw rails over roads. We draw the sprite with fewer
       * connections first - it is not clear why. */
      if (rail_semi_count > rail_card_count) {
	if (rail_card_tileno != 0) {
	  *sprs++ = sprites.rail.cardinal[rail_card_tileno];
	}
	if (rail_semi_tileno != 0 && draw_diagonal_roads) {
	  *sprs++ = sprites.rail.diagonal[rail_semi_tileno];
	}
      } else {
	if (rail_semi_tileno != 0 && draw_diagonal_roads) {
	  *sprs++ = sprites.rail.diagonal[rail_semi_tileno];
	}
	if (rail_card_tileno != 0) {
	  *sprs++ = sprites.rail.cardinal[rail_card_tileno];
	}
      }
    }

    /* Draw isolated rail/road separately. */
    if (rail) {
      int adjacent = rail_card_tileno;
      if (draw_diagonal_roads) {
	adjacent |= rail_semi_tileno;
      }
      if (adjacent == 0) {
	*sprs++ = sprites.rail.isolated;
      }
    } else if (road) {
      int adjacent = rail_card_tileno | road_card_tileno;
      if (draw_diagonal_roads) {
	adjacent |= (rail_semi_tileno | road_semi_tileno);
      }
      if (adjacent == 0) {
	*sprs++ = sprites.road.isolated;
      }
    }
  }

  /* Draw rail corners over roads. */
  sprs += fill_rail_corner_sprites(sprs, rail, rail_near);

  return sprs - saved_sprs;
}

/**********************************************************************
Fill in the sprite array for the tile at position (abs_x0,abs_y0).
Does not fill in the city or unit; that have to be done seperatly in
isometric view. Also, no fog here.

A return of -1 means the tile should be black.

The sprites are drawn in the following order:
 1) basic terrain type + irrigation/farmland (+ river hack)
 2) mine
 3) specials
 4) road/railroad
 5) huts
***********************************************************************/
int fill_tile_sprite_array_iso(struct Sprite **sprs, struct Sprite **coasts,
			       struct Sprite **dither,
			       int x, int y, bool citymode,
			       int *solid_bg)
{
  enum tile_terrain_type ttype, ttype_near[8];
  enum tile_special_type tspecial, tspecial_near[8];
  int tileno, dir, i;
  struct city *pcity;
  struct Sprite **save_sprs = sprs;

  *solid_bg = 0;

  if (tile_get_known(x, y) == TILE_UNKNOWN)
    return -1;

  pcity = map_get_city(x, y);

  build_tile_data(x, y, &ttype, &tspecial, ttype_near, tspecial_near);

  if (draw_terrain) {
    if (is_ocean(ttype)) {
      /* painted via coasts. */

      for (dir = 0; dir < 4; dir++) {
	if (contains_special(tspecial_near[DIR4_TO_DIR8[dir]], S_RIVER))
    	  *sprs++ = sprites.tx.river_outlet[dir];
      }
    } else {
      *sprs++ = get_tile_type(ttype)->sprite[0];

      switch (ttype) {
        case T_HILLS:
        tileno = INDEX_NSEW(can_blend_hills_and_mountains(T_HILLS,
						  ttype_near[DIR8_NORTH]),
			    can_blend_hills_and_mountains(T_HILLS,
						  ttype_near[DIR8_SOUTH]),
			    can_blend_hills_and_mountains(T_HILLS,
						  ttype_near[DIR8_EAST]),
			    can_blend_hills_and_mountains(T_HILLS,
						  ttype_near[DIR8_WEST]));
        *sprs++ = sprites.tx.spec_hill[tileno];
        break;
 
        case T_FOREST:
        tileno = INDEX_NSEW(ttype_near[DIR8_NORTH] == T_FOREST,
        		  ttype_near[DIR8_SOUTH] == T_FOREST,
        		  ttype_near[DIR8_EAST] == T_FOREST,
        		  ttype_near[DIR8_WEST] == T_FOREST);
        *sprs++ = sprites.tx.spec_forest[tileno];
        break;
 
        case T_MOUNTAINS:
        tileno = INDEX_NSEW(can_blend_hills_and_mountains(T_MOUNTAINS,
						  ttype_near[DIR8_NORTH]),
			    can_blend_hills_and_mountains(T_MOUNTAINS,
						  ttype_near[DIR8_SOUTH]),
			    can_blend_hills_and_mountains(T_MOUNTAINS,
						  ttype_near[DIR8_EAST]),
			    can_blend_hills_and_mountains(T_MOUNTAINS,
						  ttype_near[DIR8_WEST]));
        *sprs++ = sprites.tx.spec_mountain[tileno];
        break;

    	default:
    	break;
      }
     
      if (contains_special(tspecial, S_IRRIGATION) && !pcity && draw_irrigation) {
	if (contains_special(tspecial, S_FARMLAND))
	  *sprs++ = sprites.tx.farmland;
	else
	  *sprs++ = sprites.tx.irrigation;
      }
 
      if (contains_special(tspecial, S_RIVER)) {
	tileno = INDEX_NSEW(contains_special(tspecial_near[DIR8_NORTH], S_RIVER)
      	      	      	    || is_ocean(ttype_near[DIR8_NORTH]),
			    contains_special(tspecial_near[DIR8_SOUTH], S_RIVER)
			    || is_ocean(ttype_near[DIR8_SOUTH]),
			    contains_special(tspecial_near[DIR8_EAST], S_RIVER)
			    || is_ocean(ttype_near[DIR8_EAST]),
			    contains_special(tspecial_near[DIR8_WEST], S_RIVER)
			    || is_ocean(ttype_near[DIR8_WEST]));
      	*sprs++ = sprites.tx.spec_river[tileno];
      }
    }
  } else {
    *solid_bg = 1;

    if (contains_special(tspecial, S_IRRIGATION) && !pcity && draw_irrigation) {
      if (contains_special(tspecial, S_FARMLAND))
      	*sprs++ = sprites.tx.farmland;
      else
        *sprs++ = sprites.tx.irrigation;
    }
  }

  if (contains_special(tspecial, S_MINE) && draw_mines
      && (ttype == T_HILLS || ttype == T_MOUNTAINS)) {
    /* Oil mines come later. */
    *sprs++ = sprites.tx.mine;
  }

  if (contains_special(tspecial, S_FORTRESS) && draw_fortress_airbase) {
    *sprs++ = sprites.tx.fortress_back;
  }

  if (draw_specials) {
    if (contains_special(tspecial, S_SPECIAL_1))
      *sprs++ = tile_types[ttype].special[0].sprite;
    else if (contains_special(tspecial, S_SPECIAL_2))
      *sprs++ = tile_types[ttype].special[1].sprite;
  }

  if (contains_special(tspecial, S_MINE) && draw_mines
      && ttype != T_HILLS && ttype != T_MOUNTAINS) {
    /* Must be Glacier or Dessert. The mine sprite looks better on top
       of special. */
    *sprs++ = sprites.tx.oil_mine;
  }

  if (is_ocean(ttype)) {
    const int dirs[4] = {
      /* up */
      DIR8_NORTHWEST,
      /* down */
      DIR8_SOUTHEAST,
      /* left */
      DIR8_SOUTHWEST,
      /* right */
      DIR8_NORTHEAST
    };

    /* put coasts */
    for (i = 0; i < 4; i++) {
      int array_index = ((!is_ocean(ttype_near[dir_ccw(dirs[i])]) ? 1 : 0)
			 + (!is_ocean(ttype_near[dirs[i]]) ? 2 : 0)
			 + (!is_ocean(ttype_near[dir_cw(dirs[i])]) ? 4 : 0));

      coasts[i] = sprites.tx.coast_cape_iso[array_index][i];
    }
  } else {
    sprs += fill_road_rail_sprite_array(sprs,
					tspecial, tspecial_near, pcity);

    if (contains_special(tspecial, S_HUT) && draw_specials)
      *sprs++ = sprites.tx.village;

    /* These are drawn later in isometric view (on top of city.)
       if (contains_special(tspecial, S_POLLUTION)) *sprs++ = sprites.tx.pollution;
       if (contains_special(tspecial, S_FALLOUT)) *sprs++ = sprites.tx.fallout;
    */
  }

  /*
   * We want to mark unknown tiles so that an unreal tile will be
   * given the same marking as our current tile - that way we won't
   * get the "unknown" dither along the edge of the map.
   */
  for (dir = 0; dir < 4; dir++) {
    int x1, y1, other;

    if (MAPSTEP(x1, y1, x, y, DIR4_TO_DIR8[dir]))
      other = (tile_get_known(x1, y1) != TILE_UNKNOWN) ? ttype_near[DIR4_TO_DIR8[dir]]:T_UNKNOWN;
    else
      other = ttype_near[dir];
    dither[dir] = get_dither(ttype, other);
  }

  return sprs - save_sprs;
}

/**********************************************************************
  Fill in the sprite array for the tile at position (abs_x0,abs_y0)

The sprites are drawn in the following order:
 1) basic terrain type
 2) river
 3) irritation
 4) road/railroad
 5) specials
 6) mine
 7) hut
 8) fortress
 9) airbase
10) pollution
11) fallout
12) FoW
***********************************************************************/
int fill_tile_sprite_array(struct Sprite **sprs, int abs_x0, int abs_y0,
			   bool citymode, int *solid_bg, struct player **pplayer)
{
  enum tile_terrain_type ttype, ttype_near[8];
  enum tile_special_type tspecial, tspecial_near[8];

  int dir;
  int tileno;
  struct tile *ptile;
  struct Sprite *mysprite;
  struct city *pcity;
  struct unit *pfocus;
  struct unit *punit;
  int den_y=map.ysize*.24;

  struct Sprite **save_sprs=sprs;
  *solid_bg = 0;
  *pplayer = NULL;

  ptile=map_get_tile(abs_x0, abs_y0);

  if (tile_get_known(abs_x0, abs_y0) == TILE_UNKNOWN) {
    return 0;
  }

  pcity=map_get_city(abs_x0, abs_y0);
  pfocus=get_unit_in_focus();

  if(!flags_are_transparent || solid_color_behind_units) {
    /* non-transparent flags -> just draw city or unit */

    punit = get_drawable_unit(abs_x0, abs_y0, citymode);
    if (punit && (draw_units || (draw_focus_unit && pfocus == punit))) {
      sprs += fill_unit_sprite_array(sprs, punit, solid_bg);
      *pplayer = unit_owner(punit);
      if (unit_list_size(&ptile->units) > 1)  
	*sprs++ = sprites.unit.stack;
      return sprs - save_sprs;
    }

    if (pcity && draw_cities) {
      sprs+=fill_city_sprite_array(sprs, pcity, solid_bg);
      *pplayer = city_owner(pcity);
      return sprs - save_sprs;
    }
  }

  build_tile_data(abs_x0, abs_y0, 
		  &ttype, &tspecial, ttype_near, tspecial_near);

  if(map.is_earth &&
     abs_x0>=34 && abs_x0<=36 && abs_y0>=den_y && abs_y0<=den_y+1) {
    mysprite = sprites.tx.denmark[abs_y0-den_y][abs_x0-34];
  } else {
    tileno = INDEX_NSEW(ttype_near[DIR8_NORTH] == ttype,
			ttype_near[DIR8_SOUTH] == ttype,
			ttype_near[DIR8_EAST] == ttype,
			ttype_near[DIR8_WEST] == ttype);
    if(ttype==T_RIVER) {
      tileno |= INDEX_NSEW(is_ocean(ttype_near[DIR8_NORTH]),
			   is_ocean(ttype_near[DIR8_SOUTH]),
			   is_ocean(ttype_near[DIR8_EAST]),
			   is_ocean(ttype_near[DIR8_WEST]));
    }
    mysprite = get_tile_type(ttype)->sprite[tileno];
  }

  if (draw_terrain)
    *sprs++=mysprite;
  else
    *solid_bg = 1;

  if(is_ocean(ttype) && draw_terrain) {
    int dir;

    tileno = INDEX_NSEW(is_ocean(ttype_near[DIR8_NORTH])
			&& is_ocean(ttype_near[DIR8_EAST])
			&& !is_ocean(ttype_near[DIR8_NORTHEAST]),
			is_ocean(ttype_near[DIR8_SOUTH])
			&& is_ocean(ttype_near[DIR8_WEST])
			&& !is_ocean(ttype_near[DIR8_SOUTHWEST]),
			is_ocean(ttype_near[DIR8_EAST])
			&& is_ocean(ttype_near[DIR8_SOUTH])
			&& !is_ocean(ttype_near[DIR8_SOUTHEAST]),
			is_ocean(ttype_near[DIR8_NORTH])
			&& is_ocean(ttype_near[DIR8_WEST])
			&& !is_ocean(ttype_near[DIR8_NORTHWEST]));
    if(tileno!=0)
      *sprs++ = sprites.tx.coast_cape[tileno];

    for (dir = 0; dir < 4; dir++) {
      if (contains_special(tspecial_near[DIR4_TO_DIR8[dir]], S_RIVER) ||
          ttype_near[DIR4_TO_DIR8[dir]] == T_RIVER) {
	*sprs++ = sprites.tx.river_outlet[dir];
      }
    }
  }

  if (contains_special(tspecial, S_RIVER) && draw_terrain) {
    tileno = INDEX_NSEW(contains_special(tspecial_near[DIR8_NORTH], S_RIVER)
			|| is_ocean(ttype_near[DIR8_NORTH]),
			contains_special(tspecial_near[DIR8_SOUTH], S_RIVER)
			|| is_ocean(ttype_near[DIR8_SOUTH]),
			contains_special(tspecial_near[DIR8_EAST], S_RIVER)
			|| is_ocean(ttype_near[DIR8_EAST]),
			contains_special(tspecial_near[DIR8_WEST], S_RIVER)
			|| is_ocean(ttype_near[DIR8_WEST]));
    *sprs++=sprites.tx.spec_river[tileno];
  }

  if(contains_special(tspecial, S_IRRIGATION) && draw_irrigation) {
    if(contains_special(tspecial, S_FARMLAND)) *sprs++=sprites.tx.farmland;
    else *sprs++=sprites.tx.irrigation;
  }

  sprs += fill_road_rail_sprite_array(sprs, tspecial, tspecial_near, pcity);

  if(draw_specials) {
    if (contains_special(tspecial, S_SPECIAL_1))
      *sprs++ = tile_types[ttype].special[0].sprite;
    else if (contains_special(tspecial, S_SPECIAL_2))
      *sprs++ = tile_types[ttype].special[1].sprite;
  }

  if(contains_special(tspecial, S_MINE) && draw_mines) {
    if(ttype==T_HILLS || ttype==T_MOUNTAINS)
      *sprs++ = sprites.tx.mine;
    else /* desert */
      *sprs++ = sprites.tx.oil_mine;
  }

  if(contains_special(tspecial, S_HUT) && draw_specials) *sprs++ = sprites.tx.village;
  if(contains_special(tspecial, S_FORTRESS) && draw_fortress_airbase) *sprs++ = sprites.tx.fortress;
  if(contains_special(tspecial, S_AIRBASE) && draw_fortress_airbase) *sprs++ = sprites.tx.airbase;
  if(contains_special(tspecial, S_POLLUTION) && draw_pollution) *sprs++ = sprites.tx.pollution;
  if(contains_special(tspecial, S_FALLOUT) && draw_pollution) *sprs++ = sprites.tx.fallout;
  if(tile_get_known(abs_x0,abs_y0) == TILE_KNOWN_FOGGED && draw_fog_of_war)
    *sprs++ = sprites.tx.fog;

  if (!citymode) {
    /* 
     * We're looking to find the INDEX_NSEW for the directions that
     * are unknown.  We want to mark unknown tiles so that an unreal
     * tile will be given the same marking as our current tile - that
     * way we won't get the "unknown" dither along the edge of the
     * map.
     */
    bool known[4];

    for (dir = 0; dir < 4; dir++) {
      int x1, y1;

      if (MAPSTEP(x1, y1, abs_x0, abs_y0, DIR4_TO_DIR8[dir]))
        known[dir] = (tile_get_known(x1, y1) != TILE_UNKNOWN);
      else
        known[dir] = TRUE;
    }

    tileno =
	INDEX_NSEW(!known[DIR4_NORTH], !known[DIR4_SOUTH],
		   !known[DIR4_EAST], !known[DIR4_WEST]);

    if (tileno != 0) 
      *sprs++ = sprites.tx.darkness[tileno];
  }

  if (flags_are_transparent) {
    int dummy;
    /* transparent flags -> draw city or unit last */

    if (pcity && draw_cities) {
      sprs+=fill_city_sprite_array(sprs, pcity, &dummy);
    }

    punit = find_visible_unit(ptile);
    if (punit) {
      if (!citymode || punit->owner != game.player_idx) {
	if ((!focus_unit_hidden || pfocus != punit) &&
	    (draw_units || (draw_focus_unit && !focus_unit_hidden && punit == pfocus))) {
	  no_backdrop = (pcity != NULL);
	  sprs += fill_unit_sprite_array(sprs, punit, &dummy);
	  no_backdrop=FALSE;
	  if (unit_list_size(&ptile->units)>1)  
	    *sprs++ = sprites.unit.stack;
	}
      }
    }
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
    sp = hash_lookup_data(sprite_hash, buffer);
    if (is_isometric) {
      my_snprintf(buffer, sizeof(buffer_wall), "%s_%d_wall", graphics, j);
      sp_wall = hash_lookup_data(sprite_hash, buffer);
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
    sp = hash_lookup_data(sprite_hash, buffer);
    if (sp) {
      sprites.city.tile[style][city_styles[style].tiles_num] = sp;
    } else {
      freelog(LOG_NORMAL, "Warning: no wall tile for graphic %s", graphics);
    }
  }

  /* occupied tile */
  my_snprintf(buffer, sizeof(buffer), "%s_occupied", graphics);
  sp = hash_lookup_data(sprite_hash, buffer);
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

    sprites.city.tile[style][0] =
      hash_lookup_data(sprite_hash, "cd.city");
    sprites.city.tile[style][1] =
      hash_lookup_data(sprite_hash, "cd.city_wall");
    sprites.city.tile[style][2] =
      hash_lookup_data(sprite_hash, "cd.occupied");
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
enum color_std overview_tile_color(int x, int y)
{
  enum color_std color;
  struct tile *ptile=map_get_tile(x, y);
  struct unit *punit;
  struct city *pcity;

  if (tile_get_known(x, y) == TILE_UNKNOWN) {
    color=COLOR_STD_BLACK;
  } else if((pcity=map_get_city(x, y))) {
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
    if (tile_get_known(x, y) == TILE_KNOWN_FOGGED && draw_fog_of_war) {
      color = COLOR_STD_RACE4;
    } else {
      color = COLOR_STD_OCEAN;
    }
  } else {
    if (tile_get_known(x, y) == TILE_KNOWN_FOGGED && draw_fog_of_war) {
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
struct unit *get_drawable_unit(int x, int y, bool citymode)
{
  struct unit *punit = find_visible_unit(map_get_tile(x, y));
  struct unit *pfocus = get_unit_in_focus();

  if (!punit)
    return NULL;

  if (citymode && punit->owner == game.player_idx)
    return NULL;

  if (punit != pfocus
      || !focus_unit_hidden
      || !same_pos(punit->x, punit->y, pfocus->x, pfocus->y))
    return punit;
  else
    return NULL;
}

/**********************************************************************
...
***********************************************************************/
static int my_cmp(const void *a1, const void *a2)
{
  const struct Sprite **b1 = (const struct Sprite **) a1;
  const struct Sprite **b2 = (const struct Sprite **) a2;

  const struct Sprite *c1 = *b1;
  const struct Sprite *c2 = *b2;

  if (c1 < c2) {
    return -1;
  }
  if (c2 < c1) {
    return 1;
  }
  return 0;
}

/**********************************************************************
...
***********************************************************************/
void tilespec_free_tiles(void)
{
  int i, entries = hash_num_entries(sprite_hash);
  struct Sprite **sprites = fc_malloc(sizeof(*sprites) * entries);
  struct Sprite *last_sprite = NULL;

  freelog(LOG_DEBUG, "tilespec_free_tiles");

  for (i = 0; i < entries; i++) {
    const char *key = hash_key_by_number(sprite_hash, 0);

    sprites[i] = hash_delete_entry(sprite_hash, key);
    free((void *) key);
  }

  qsort(sprites, entries, sizeof(sprites[0]), my_cmp);

  for (i = 0; i < entries; i++) {
    if (sprites[i] == last_sprite) {
      continue;
    }

    last_sprite = sprites[i];
    free_sprite(sprites[i]);
  }

  free(sprites);

  hash_free(sprite_hash);
  sprite_hash = NULL;
}

/**************************************************************************
  Return a sprite for the given citizen.  The citizen's type is given,
  as well as their index (in the range [0..pcity->size)).  The
  citizen's city can be used to determine which sprite to use (a NULL
  value indicates there is no city; i.e., the sprite is just being
  used as a picture).
**************************************************************************/
struct Sprite *get_citizen_sprite(enum citizen_type type, int citizen_index,
				  struct city *pcity)
{
  assert(type >= 0 && type < NUM_TILES_CITIZEN);
  citizen_index %= sprites.citizen[type].count;
  return sprites.citizen[type].sprite[citizen_index];
}
