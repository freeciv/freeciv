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

#include <stdio.h>
#include <string.h>
#include <assert.h>

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
#include "sbuffer.h"
#include "shared.h"
#include "support.h"
#include "unit.h"

#include "control.h" /* for fill_xxx */
#include "graphics_g.h"
#include "options.h" /* for fill_xxx */

#include "tilespec.h"

char *main_intro_filename;
char *minimap_intro_filename;

struct named_sprites sprites;

int NORMAL_TILE_WIDTH;
int NORMAL_TILE_HEIGHT;
int UNIT_TILE_WIDTH;
int UNIT_TILE_HEIGHT;
int SMALL_TILE_WIDTH;
int SMALL_TILE_HEIGHT;

int is_isometric;

char *city_names_font;
char *city_productions_font_name;

int flags_are_transparent=1;

int num_tiles_explode_unit=0;


static int num_spec_files = 0;
static char **spec_filenames;	/* full pathnames */

static struct hash_table *sprite_hash = 0;
/*
   This hash table sprite_hash maps tilespec tags to tile Sprite pointers.
   This is kept permanently after setup, for doing lookups on ruleset
   information (including on reconnections etc).
*/

static struct sbuffer *sprite_key_sb;
/* used to allocate keys for sprite_hash */

#define TILESPEC_CAPSTR "+tilespec2 duplicates_ok"
/*
   Tilespec capabilities acceptable to this program:
   +tilespec2     -  basic format, required
   duplicates_ok  -  we can handle existence of duplicate tags
                     (lattermost tag which appears is used; tilesets which
		     have duplicates should specify "+duplicates_ok")
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
static int focus_unit_hidden = 0;

/*
  If no_backdrop is true, then no color/flag is drawn behind the city/unit.
*/
static int no_backdrop = 0;

/**********************************************************************
  Gets full filename for tilespec file, based on input name.
  Returned data is allocated, and freed by user as required.
  Input name may be null, in which case uses default.
  Falls back to default if can't find specified name;
  dies if can't find default.
***********************************************************************/
static char *tilespec_fullname(const char *tileset_name)
{
  char *tileset_default;
  char *fname, *dname;

  if (isometric_view_supported()) {
    tileset_default = "hires";    /* Do not i18n! --dwp */
  } else {
    tileset_default = "trident";    /* Do not i18n! --dwp */
  }

  if (!tileset_name) {
    tileset_name = tileset_default;
  }
  fname = fc_malloc(strlen(tileset_name)+16);
  sprintf(fname, "%s.tilespec", tileset_name);
  
  dname = datafilename(fname);
  if (dname) {
    free(fname);
    return mystrdup(dname);
  }

  if (strcmp(tileset_name, tileset_default) == 0) {
    freelog(LOG_FATAL, _("No useable default tileset found, aborting!"));
    exit(1);
  }
  freelog(LOG_ERROR, _("Trying \"%s\" tileset."), tileset_default);
  free(fname);
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
    exit(1);
  }
  if (!has_capabilities(file_capstr, us_capstr)) {
    freelog(LOG_FATAL, _("%s file claims required option(s)"
			 " which we don't support:"), which);
    freelog(LOG_FATAL, _("file: \"%s\""), filename);
    freelog(LOG_FATAL, _("file options: %s"), file_capstr);
    freelog(LOG_FATAL, _("supported options: %s"), us_capstr);
    exit(1);
  }
}

/**********************************************************************
  Returns the correct name of the gfx file (with path and extension)
  Must be free'd when no longer used
***********************************************************************/
static char *tilespec_gfx_filename(const char *gfx_filename)
{
  char **gfx_fileexts;
  char *full_name,*real_full_name,*gfx_current_fileext;

  gfx_fileexts = gfx_fileextensions();

  while((gfx_current_fileext = *gfx_fileexts++))
  {
    full_name = fc_malloc(strlen(gfx_filename)+strlen(gfx_current_fileext)+2);
    sprintf(full_name,"%s.%s",gfx_filename,gfx_current_fileext);

    real_full_name = datafilename(full_name);
    free(full_name);
    if(real_full_name) return mystrdup(real_full_name);
  }

  freelog(LOG_FATAL, _("Couldn't find a supported gfx file extension for %s"),
	  gfx_filename);
  exit(1);
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
    exit(1);
  }
  check_tilespec_capabilities(file, "tilespec", TILESPEC_CAPSTR, fname);

  section_file_lookup(file, "tilespec.name"); /* currently unused */

  is_isometric = secfile_lookup_int_default(file, 0, "tilespec.is_isometric");
  if (is_isometric && !isometric_view_supported()) {
    freelog(LOG_ERROR, _("Client does not support isometric tilesets."
	    " Using default tileset instead."));
    assert(tileset_name);
    section_file_free(file);
    free(fname);
    tilespec_read_toplevel(NULL);
    return;
  }
  if (!is_isometric && !overhead_view_supported()) {
    freelog(LOG_ERROR, _("Client does not support overhead view tilesets."
	    " Using default tileset instead."));
    assert(tileset_name);
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

  c = secfile_lookup_str_default(file, "10x20", "tilespec.city_names_font");
  city_names_font = mystrdup(c);

  c =
      secfile_lookup_str_default(file, "8x16",
				 "tilespec.city_productions_font");
  city_productions_font_name = mystrdup(c);

  flags_are_transparent =
    secfile_lookup_int(file, "tilespec.flags_are_transparent"); 

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
    exit(1);
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
  char *gfx_filename,*gfx_current_fileext;
  char **gridnames, **tags, **gfx_fileexts;
  int num_grids, num_tags;
  int i, j, k;
  int x_top_left, y_top_left, dx, dy;
  int row, column;
  int x1, y1;

  freelog(LOG_DEBUG, "loading spec %s", spec_filename);
  if (!section_file_load(file, spec_filename)) {
    freelog(LOG_FATAL, _("Could not open \"%s\"."), spec_filename);
    exit(1);
  }
  check_tilespec_capabilities(file, "spec", SPEC_CAPSTR, spec_filename);

  section_file_lookup(file, "info.artists"); /* currently unused */

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
    exit(1);
  }


  gridnames = secfile_get_secnames_prefix(file, "grid_", &num_grids);
  if (num_grids==0) {
    freelog(LOG_FATAL, "spec %s has no grid_* sections", spec_filename);
    exit(1);
  }

  for(i=0; i<num_grids; i++) {
    int is_pixel_border =
      secfile_lookup_int_default(file, 0, "%s.is_pixel_border", gridnames[i]);
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
	hash_replace(sprite_hash, sbuf_strdup(sprite_key_sb, tags[k]),
		     small_sprite);
      }
      free(tags);
    }
  }
  free(gridnames);

  free_sprite(big_sprite);
  
  section_file_check_unused(file, spec_filename);
  section_file_free(file);
  freelog(LOG_DEBUG, "finished %s", spec_filename);
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
#define SET_SPRITE(field, tag) \
    sprites.field = hash_lookup_data(sprite_hash, tag);

/**********************************************************************
  Initialize 'sprites' structure based on hardwired tags which
  freeciv always requires. 
***********************************************************************/
static void tilespec_lookup_sprite_tags(void)
{
  char buffer[512];
  const char dir_char[] = "nsew";
  int i, j;
  
  assert(sprite_hash);

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

  /* This uses internal code for citizens array/index: */
  SET_SPRITE(citizen[0], "citizen.entertainer");
  SET_SPRITE(citizen[1], "citizen.scientist");
  SET_SPRITE(citizen[2], "citizen.tax_collector");
  SET_SPRITE(citizen[3], "citizen.content_0");
  SET_SPRITE(citizen[4], "citizen.content_1");
  SET_SPRITE(citizen[5], "citizen.happy_0");
  SET_SPRITE(citizen[6], "citizen.happy_1");
  SET_SPRITE(citizen[7], "citizen.unhappy_0");
  SET_SPRITE(citizen[8], "citizen.unhappy_1");

  SET_SPRITE(spaceship.solar_panels, "spaceship.solar_panels");
  SET_SPRITE(spaceship.life_support, "spaceship.life_support");
  SET_SPRITE(spaceship.habitation,   "spaceship.habitation");
  SET_SPRITE(spaceship.structural,   "spaceship.structural");
  SET_SPRITE(spaceship.fuel,         "spaceship.fuel");
  SET_SPRITE(spaceship.propulsion,   "spaceship.propulsion");

  SET_SPRITE(road.isolated, "r.road_isolated");
  SET_SPRITE(rail.isolated, "r.rail_isolated");
  
  if (is_isometric) {
    for (i=0; i<8; i++) {
      my_snprintf(buffer, sizeof(buffer), "r.road%d", i);
      SET_SPRITE(road.dir[i], buffer);
      my_snprintf(buffer, sizeof(buffer), "r.rail%d", i);
      SET_SPRITE(rail.dir[i], buffer);
    }
  } else {
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
  if (!is_isometric) {
    SET_SPRITE(tx.oil_mine,   "tx.oil_mine");
  }
  SET_SPRITE(tx.pollution,  "tx.pollution");
  SET_SPRITE(tx.village,    "tx.village");
  SET_SPRITE(tx.fortress,   "tx.fortress");
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

  sprites.city.tile_wall = 0;       /* no place to initialize this variable */
  sprites.city.tile = 0;            /* no place to initialize this variable */
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
  sprite_key_sb = sbuf_new();

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
					    int required, const char *what,
					    const char *name)
{
  struct Sprite *sp;
  int loglevel = required ? LOG_NORMAL : LOG_DEBUG;
  
  /* (should get sprite_hash before connection) */
  if (!sprite_hash) {
    freelog(LOG_FATAL, "attempt to lookup for %s %s before sprite_hash setup",
	    what, name);
    exit(1);
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
      tt->sprite[0] = lookup_sprite_tag_alt(buffer1, NULL, 1, "tile_type",
					    tt->terrain_name);
    } else {
      tt->sprite[0] = NULL;
    }
  } else {
    for(i=0; i<NUM_DIRECTION_NSEW; i++) {
      nsew = nsew_str(i);
      my_snprintf(buffer1, sizeof(buffer1), "%s_%s", tt->graphic_str, nsew);
      my_snprintf(buffer2, sizeof(buffer2), "%s_%s", tt->graphic_alt, nsew);

      tt->sprite[i] = lookup_sprite_tag_alt(buffer1, buffer2, 1, "tile_type",
					    tt->terrain_name);

      assert(tt->sprite[i]);
      /* should probably do something if NULL, eg generic default? */
    }
  }

  for (i=0; i<2; i++) {
    char *name = i ? tt->special_2_name : tt->special_1_name;
    if (name[0]) {
      tt->special[i].sprite
	= lookup_sprite_tag_alt(tt->special[i].graphic_str,
				tt->special[i].graphic_alt,
				name[0], "tile_type special", name);
      assert(tt->special[i].sprite);
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
				      1, "government", gov->name);
  
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
					    1, "nation", this_nation->name);

  /* should probably do something if NULL, eg generic default? */
}

/**********************************************************************
  ...
***********************************************************************/
static struct Sprite *get_city_nation_flag_sprite(struct city *pcity)
{
  return get_nation_by_plr(&game.players[pcity->owner])->flag_sprite;
}

/**********************************************************************
 ...
***********************************************************************/
static struct Sprite *get_unit_nation_flag_sprite(struct unit *punit)
{
  return get_nation_by_plr(&game.players[punit->owner])->flag_sprite;
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
  struct tile *ptile = map_get_tile(pcity->x, pcity->y);

  *solid_bg = 0;
  if (!no_backdrop) {
    if(!solid_color_behind_units) {
      /* will be the first sprite if flags_are_transparent == FALSE */
      *sprs++ = get_city_nation_flag_sprite(pcity);
    } else {
      *solid_bg = 1;
    }
  }

  if (genlist_size(&(ptile->units.list)) > 0)
    *sprs++ = get_city_occupied_sprite(pcity);

  *sprs++ = get_city_sprite(pcity);

  if (city_got_citywalls(pcity))
    *sprs++ = get_city_wall_sprite(pcity);

  if(map_get_special(pcity->x, pcity->y) & S_POLLUTION)
    *sprs++ = sprites.tx.pollution;
  if(map_get_special(pcity->x, pcity->y) & S_FALLOUT)
    *sprs++ = sprites.tx.fallout;

  if(city_unhappy(pcity))
    *sprs++ = sprites.city.disorder;

  if(ptile->known==TILE_KNOWN_FOGGED && draw_fog_of_war)
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
  struct tile *ptile = map_get_tile(pcity->x, pcity->y);

  if (!no_backdrop) {
    *sprs++ = get_city_nation_flag_sprite(pcity);
  }

  if (genlist_size(&(ptile->units.list)) > 0)
    *sprs++ = get_city_occupied_sprite(pcity);

  *sprs++ = get_city_sprite(pcity);

  if (city_unhappy(pcity))
    *sprs++ = sprites.city.disorder;

  return sprs - save_sprs;
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

  *sprs++ = get_unit_type(punit->type)->sprite;

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

  if(punit->ai.control) {
    if(is_military_unit(punit)) {
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

  ihp = ((NUM_TILES_HP_BAR-1)*punit->hp) / get_unit_type(punit->type)->hp;
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

  if (ttype == T_OCEAN || ttype == T_JUNGLE)
    return NULL;

  if (ttype_other == T_OCEAN)
    return sprites.coast_color;

  if (ttype_other != T_UNKNOWN && ttype_other != T_LAST)
    return get_tile_type(ttype_other)->sprite[0];
  else
    return NULL;
}

/**********************************************************************
Fill in the sprite array for the tile at position (abs_x0,abs_y0).
Does not fill in the city or unit; that have to be done seperatly in
isometric view. Also, no fog here.

A return of -1 means the tile should be black.
***********************************************************************/
int fill_tile_sprite_array_iso(struct Sprite **sprs, struct Sprite **coasts,
			       struct Sprite **dither,
			       int x, int y, int citymode,
			       int *solid_bg)
{
  int ttype, ttype_near[8];
  int tspecial, tspecial_near[8];
  int ttype_north, ttype_south, ttype_east, ttype_west;
  int ttype_north_east, ttype_south_east, ttype_south_west, ttype_north_west;
  int tspecial_north, tspecial_south, tspecial_east, tspecial_west;
  int tspecial_north_east, tspecial_south_east, tspecial_south_west, tspecial_north_west;

  int tileno;
  struct tile *ptile;
  struct city *pcity;
  struct Sprite **save_sprs = sprs;
  int dir, i;

  *solid_bg = 0;

  if (!normalize_map_pos(&x, &y))
    return -1;

  ptile = map_get_tile(x, y);
  if (!ptile->known)
    return -1;

  pcity = ptile->city;
  ttype = map_get_terrain(x, y);
  tspecial = map_get_special(x, y);

  /* A little hack to avoid drawing seperate T_RIVER isometric tiles. */
  if (ttype == T_RIVER) {
    ttype = T_GRASSLAND;
    tspecial |= S_RIVER;
  }

  for (dir=0; dir<8; dir++) {
    int x1 = x + DIR_DX2[dir];
    int y1 = y + DIR_DY2[dir];
    if (normalize_map_pos(&x1, &y1)) {
      ttype_near[dir] = map_get_terrain(x1, y1);
      tspecial_near[dir] = map_get_special(x1, y1);

      /* hacking away the river here... */
      if (ttype_near[dir] == T_RIVER) {
	ttype_near[dir] = T_GRASSLAND;
	tspecial_near[dir] |= S_RIVER;
      }
    } else {
      ttype_near[dir] = T_UNKNOWN;
      tspecial_near[dir] = S_NO_SPECIAL;
    }
  }
  ttype_north      = ttype_near[0];
  ttype_north_east = ttype_near[1];
  ttype_east       = ttype_near[2];
  ttype_south_east = ttype_near[3];
  ttype_south      = ttype_near[4];
  ttype_south_west = ttype_near[5];
  ttype_west       = ttype_near[6];
  ttype_north_west = ttype_near[7];
  tspecial_north      = tspecial_near[0];
  tspecial_north_east = tspecial_near[1];
  tspecial_east       = tspecial_near[2];
  tspecial_south_east = tspecial_near[3];
  tspecial_south      = tspecial_near[4];
  tspecial_south_west = tspecial_near[5];
  tspecial_west       = tspecial_near[6];
  tspecial_north_west = tspecial_near[7];
  
  if (draw_terrain) {
    if (ttype != T_OCEAN) /* painted via coasts. */
      *sprs++ = get_tile_type(ttype)->sprite[0];

    if (ttype == T_HILLS) {
      tileno = INDEX_NSEW((ttype_north==T_HILLS || ttype_north==T_HILLS),
			  (ttype_south==T_HILLS || ttype_south==T_HILLS),
			  (ttype_east==T_HILLS || ttype_east==T_HILLS),
			  (ttype_west==T_HILLS || ttype_west==T_HILLS));
      *sprs++=sprites.tx.spec_hill[tileno];
    }

    if (ttype == T_FOREST) {
      tileno = INDEX_NSEW((ttype_north==T_FOREST || ttype_north==T_FOREST),
			  (ttype_south==T_FOREST || ttype_south==T_FOREST),
			  (ttype_east==T_FOREST || ttype_east==T_FOREST),
			  (ttype_west==T_FOREST || ttype_west==T_FOREST));
      *sprs++=sprites.tx.spec_forest[tileno];
    }

    if (ttype == T_MOUNTAINS) {
      tileno = INDEX_NSEW((ttype_north==T_MOUNTAINS || ttype_north==T_MOUNTAINS),
			  (ttype_south==T_MOUNTAINS || ttype_south==T_MOUNTAINS),
			  (ttype_east==T_MOUNTAINS || ttype_east==T_MOUNTAINS),
			  (ttype_west==T_MOUNTAINS || ttype_west==T_MOUNTAINS));
      *sprs++=sprites.tx.spec_mountain[tileno];
    }

    if (tspecial&S_RIVER) {
      tileno = INDEX_NSEW((tspecial_north&S_RIVER || ttype_north==T_OCEAN),
			  (tspecial_south&S_RIVER || ttype_south==T_OCEAN),
			  (tspecial_east&S_RIVER || ttype_east==T_OCEAN),
			  (tspecial_west&S_RIVER || ttype_west==T_OCEAN));
      *sprs++=sprites.tx.spec_river[tileno];
    }

    if (ttype == T_OCEAN) {
      if(tspecial_north&S_RIVER || ttype_north==T_RIVER)
	*sprs++ = sprites.tx.river_outlet[DIR_NORTH];
      if(tspecial_west&S_RIVER || ttype_west==T_RIVER)
	*sprs++ = sprites.tx.river_outlet[DIR_WEST];
      if(tspecial_south&S_RIVER || ttype_south==T_RIVER)
	*sprs++ = sprites.tx.river_outlet[DIR_SOUTH];
      if(tspecial_east&S_RIVER || ttype_east==T_RIVER)
	*sprs++ = sprites.tx.river_outlet[DIR_EAST];
    }
  } else {
    *solid_bg = 1;
  }

  if (draw_specials) {
    if (tspecial & S_SPECIAL_1)
      *sprs++ = tile_types[ttype].special[0].sprite;
    else if (tspecial & S_SPECIAL_2)
      *sprs++ = tile_types[ttype].special[1].sprite;
  }

  if (tspecial & S_MINE && draw_mines) {
    /* We do not have an oil tower in isometric view yet... */
    *sprs++ = sprites.tx.mine;
  }

  if (tspecial & S_IRRIGATION && !pcity && draw_irrigation) {
    if (tspecial & S_FARMLAND) {
      *sprs++=sprites.tx.farmland;
    } else {
      *sprs++=sprites.tx.irrigation;
    }
  }

  if (tspecial & S_RAILROAD && draw_roads_rails) {
    int found = 0;
    for (dir=0; dir<8; dir++) {
      if (tspecial_near[dir] & S_RAILROAD) {
	*sprs++ = sprites.rail.dir[dir];
	found = 1;
      } else if (tspecial_near[dir] & S_ROAD) {
	*sprs++ = sprites.road.dir[dir];
	found = 1;
      }
    }
    if (!found && !pcity)
      *sprs++ = sprites.rail.isolated;
  } else if (tspecial & S_ROAD && draw_roads_rails) {
    int found = 0;
    for (dir=0; dir<8; dir++) {
      if (tspecial_near[dir] & S_ROAD) {
	*sprs++ = sprites.road.dir[dir];
	found = 1;
      }
    }
    if (!found && !pcity)
      *sprs++ = sprites.road.isolated;
  }

  if (tspecial & S_HUT && draw_specials) *sprs++ = sprites.tx.village;
  /* These are drawn later in isometric view (on top of city.)
     if (tspecial & S_POLLUTION) *sprs++ = sprites.tx.pollution;
     if (tspecial & S_FALLOUT) *sprs++ = sprites.tx.fallout;
  */

  /* put coasts */
  if (ttype == T_OCEAN) {
    for (i = 0; i < 4; i++) {
      int ttype_adj[3];
      int array_index;
      switch (i) {
      case 0: /* up */
	ttype_adj[0] = ttype_west;
	ttype_adj[1] = ttype_north_west;
	ttype_adj[2] = ttype_north;
	break;
      case 1: /* down */
	ttype_adj[0] = ttype_east;
	ttype_adj[1] = ttype_south_east;
	ttype_adj[2] = ttype_south;
	break;
      case 2: /* left */
	ttype_adj[0] = ttype_south;
	ttype_adj[1] = ttype_south_west;
	ttype_adj[2] = ttype_west;
	break;
      case 3: /* right*/
	ttype_adj[0] = ttype_north;
	ttype_adj[1] = ttype_north_east;
	ttype_adj[2] = ttype_east;
	break;
      default:
	abort();
      }

      array_index = (ttype_adj[0] != T_OCEAN ? 1 : 0)
	+  (ttype_adj[1] != T_OCEAN ? 2 : 0)
	+  (ttype_adj[2] != T_OCEAN ? 4 : 0);
      coasts[i] = sprites.tx.coast_cape_iso[array_index][i];
    }
  }

  dither[0] = get_dither(ttype, tile_is_known(x, y-1) ? ttype_north : T_UNKNOWN);
  dither[1] = get_dither(ttype, tile_is_known(x, y+1) ? ttype_south : T_UNKNOWN);
  dither[2] = get_dither(ttype, tile_is_known(x+1, y) ? ttype_east : T_UNKNOWN);
  dither[3] = get_dither(ttype, tile_is_known(x-1, y) ? ttype_west : T_UNKNOWN);

  return sprs - save_sprs;
}

/**********************************************************************
  Fill in the sprite array for the tile at position (abs_x0,abs_y0)
***********************************************************************/
int fill_tile_sprite_array(struct Sprite **sprs, int abs_x0, int abs_y0,
			   int citymode, int *solid_bg, struct player **pplayer)
{
  int ttype, ttype_north, ttype_south, ttype_east, ttype_west;
  int ttype_north_east, ttype_south_east, ttype_south_west, ttype_north_west;
  int tspecial, tspecial_north, tspecial_south, tspecial_east, tspecial_west;
  int tspecial_north_east, tspecial_south_east, tspecial_south_west, tspecial_north_west;
  int rail_card_tileno=0, rail_semi_tileno=0, road_card_tileno=0, road_semi_tileno=0;
  int rail_card_count=0, rail_semi_count=0, road_card_count=0, road_semi_count=0;

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

  if(abs_y0>=map.ysize || ptile->known == TILE_UNKNOWN) {
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

  ttype=map_get_terrain(abs_x0, abs_y0);
  ttype_east=map_get_terrain(abs_x0+1, abs_y0);
  ttype_west=map_get_terrain(abs_x0-1, abs_y0);

  /* make north and south pole seamless: */
  if(abs_y0==0) {
    ttype_north=ttype;
    ttype_north_east=ttype_east;
    ttype_north_west=ttype_west;
  } else {
    ttype_north=map_get_terrain(abs_x0, abs_y0-1);
    ttype_north_east=map_get_terrain(abs_x0+1, abs_y0-1);
    ttype_north_west=map_get_terrain(abs_x0-1, abs_y0-1);
  }
  if(abs_y0==map.ysize-1) {
    ttype_south=ttype;
    ttype_south_east=ttype_east;
    ttype_south_west=ttype_west;
  } else {
    ttype_south=map_get_terrain(abs_x0, abs_y0+1);
    ttype_south_east=map_get_terrain(abs_x0+1, abs_y0+1);
    ttype_south_west=map_get_terrain(abs_x0-1, abs_y0+1);
  }

  /* map_get_special() returns S_NO_SPECIAL past poles anyway */
  tspecial=map_get_special(abs_x0, abs_y0);
  tspecial_north=map_get_special(abs_x0, abs_y0-1);
  tspecial_east=map_get_special(abs_x0+1, abs_y0);
  tspecial_south=map_get_special(abs_x0, abs_y0+1);
  tspecial_west=map_get_special(abs_x0-1, abs_y0);
  tspecial_north_east=map_get_special(abs_x0+1, abs_y0-1);
  tspecial_south_east=map_get_special(abs_x0+1, abs_y0+1);
  tspecial_south_west=map_get_special(abs_x0-1, abs_y0+1);
  tspecial_north_west=map_get_special(abs_x0-1, abs_y0-1);

  if(map.is_earth &&
     abs_x0>=34 && abs_x0<=36 && abs_y0>=den_y && abs_y0<=den_y+1) {
    mysprite = sprites.tx.denmark[abs_y0-den_y][abs_x0-34];
  } else {
    tileno = INDEX_NSEW((ttype_north==ttype),
			(ttype_south==ttype),
			(ttype_east==ttype),	
			(ttype_west==ttype));
    if(ttype==T_RIVER) {
      tileno |= INDEX_NSEW((ttype_north==T_OCEAN),
			   (ttype_south==T_OCEAN),
			   (ttype_east==T_OCEAN),
			   (ttype_west==T_OCEAN));
    }
    mysprite = get_tile_type(ttype)->sprite[tileno];
  }

  if (draw_terrain) *sprs++=mysprite;
  else *solid_bg = 1;

  if(ttype==T_OCEAN && draw_terrain) {
    tileno = INDEX_NSEW((ttype_north==T_OCEAN && ttype_east==T_OCEAN && 
                         ttype_north_east!=T_OCEAN),
                        (ttype_south==T_OCEAN && ttype_west==T_OCEAN && 
                         ttype_south_west!=T_OCEAN),
                        (ttype_east==T_OCEAN && ttype_south==T_OCEAN && 
                         ttype_south_east!=T_OCEAN),
                        (ttype_north==T_OCEAN && ttype_west==T_OCEAN && 
                         ttype_north_west!=T_OCEAN));
    if(tileno!=0)
      *sprs++ = sprites.tx.coast_cape[tileno];

    if(tspecial_north&S_RIVER || ttype_north==T_RIVER)
      *sprs++ = sprites.tx.river_outlet[DIR_NORTH];
    if(tspecial_west&S_RIVER || ttype_west==T_RIVER)
      *sprs++ = sprites.tx.river_outlet[DIR_WEST];
    if(tspecial_south&S_RIVER || ttype_south==T_RIVER)
      *sprs++ = sprites.tx.river_outlet[DIR_SOUTH];
    if(tspecial_east&S_RIVER || ttype_east==T_RIVER)
      *sprs++ = sprites.tx.river_outlet[DIR_EAST];
  }

  if (tspecial&S_RIVER && draw_terrain) {
    tileno = INDEX_NSEW((tspecial_north&S_RIVER || ttype_north==T_OCEAN),
			(tspecial_south&S_RIVER || ttype_south==T_OCEAN),
			(tspecial_east&S_RIVER  || ttype_east==T_OCEAN),
			(tspecial_west&S_RIVER  || ttype_west== T_OCEAN));
    *sprs++=sprites.tx.spec_river[tileno];
  }

  if(tspecial & S_IRRIGATION && draw_irrigation) {
    if(tspecial & S_FARMLAND) *sprs++=sprites.tx.farmland;
    else *sprs++=sprites.tx.irrigation;
  }

  if(((tspecial & S_ROAD) || (tspecial & S_RAILROAD)) && draw_roads_rails) {
    int n, s, e, w;
    
    n = BOOL_VAL(tspecial_north&S_RAILROAD);
    s = BOOL_VAL(tspecial_south&S_RAILROAD);
    e = BOOL_VAL(tspecial_east&S_RAILROAD);
    w = BOOL_VAL(tspecial_west&S_RAILROAD);
    rail_card_count = n + s + e + w;
    rail_card_tileno = INDEX_NSEW(n,s,e,w);
    
    n = BOOL_VAL(tspecial_north&S_ROAD);
    s = BOOL_VAL(tspecial_south&S_ROAD);
    e = BOOL_VAL(tspecial_east&S_ROAD);
    w = BOOL_VAL(tspecial_west&S_ROAD);
    road_card_count = n + s + e + w;
    road_card_tileno = INDEX_NSEW(n,s,e,w);
    
    n = BOOL_VAL(tspecial_north_east&S_RAILROAD);
    s = BOOL_VAL(tspecial_south_west&S_RAILROAD);
    e = BOOL_VAL(tspecial_south_east&S_RAILROAD);
    w = BOOL_VAL(tspecial_north_west&S_RAILROAD);
    rail_semi_count = n + s + e + w;
    rail_semi_tileno = INDEX_NSEW(n,s,e,w);
    
    n = BOOL_VAL(tspecial_north_east&S_ROAD);
    s = BOOL_VAL(tspecial_south_west&S_ROAD);
    e = BOOL_VAL(tspecial_south_east&S_ROAD);
    w = BOOL_VAL(tspecial_north_west&S_ROAD);
    road_semi_count = n + s + e + w;
    road_semi_tileno = INDEX_NSEW(n,s,e,w);

    if(tspecial & S_RAILROAD) {
      road_card_tileno&=~rail_card_tileno;
      road_semi_tileno&=~rail_semi_tileno;
    } else if(tspecial & S_ROAD) {
      rail_card_tileno&=~road_card_tileno;
      rail_semi_tileno&=~road_semi_tileno;
    }

    if(road_semi_count > road_card_count) {
      if(road_card_tileno!=0) {
        *sprs++ = sprites.road.cardinal[road_card_tileno];
      }
      if(road_semi_tileno!=0 && draw_diagonal_roads) {
        *sprs++ = sprites.road.diagonal[road_semi_tileno];
      }
    } else {
      if(road_semi_tileno!=0 && draw_diagonal_roads) {
        *sprs++ = sprites.road.diagonal[road_semi_tileno];
      }
      if(road_card_tileno!=0) {
        *sprs++ = sprites.road.cardinal[road_card_tileno];
      }
    }

    if(rail_semi_count > rail_card_count) {
      if(rail_card_tileno!=0) {
        *sprs++ = sprites.rail.cardinal[rail_card_tileno];
      }
      if(rail_semi_tileno!=0 && draw_diagonal_roads) {
        *sprs++ = sprites.rail.diagonal[rail_semi_tileno];
      }
    } else {
      if(rail_semi_tileno!=0 && draw_diagonal_roads) {
        *sprs++ = sprites.rail.diagonal[rail_semi_tileno];
      }
      if(rail_card_tileno!=0) {
        *sprs++ = sprites.rail.cardinal[rail_card_tileno];
      }
    }
  }

  if(draw_specials) {
    if(tspecial & S_SPECIAL_1)
      *sprs++ = tile_types[ttype].special[0].sprite;
    else if(tspecial & S_SPECIAL_2)
      *sprs++ = tile_types[ttype].special[1].sprite;
  }

  if(tspecial & S_MINE && draw_mines) {
    if(ttype==T_HILLS || ttype==T_MOUNTAINS)
      *sprs++ = sprites.tx.mine;
    else /* desert */
      *sprs++ = sprites.tx.oil_mine;
  }
  
  if (draw_roads_rails) {
    if (tspecial & S_RAILROAD) {
      int adjacent = rail_card_tileno;
      if (draw_diagonal_roads)
	adjacent |= rail_semi_tileno;
      if (!adjacent)
	*sprs++ = sprites.rail.isolated;
    }
    else if (tspecial & S_ROAD) {
      int adjacent = (rail_card_tileno | road_card_tileno);
      if (draw_diagonal_roads)
	adjacent |= (rail_semi_tileno | road_semi_tileno);
      if (!adjacent)
	*sprs++ = sprites.road.isolated;
    }
  }

  if(tspecial & S_HUT && draw_specials) *sprs++ = sprites.tx.village;
  if(tspecial & S_FORTRESS && draw_fortress_airbase) *sprs++ = sprites.tx.fortress;
  if(tspecial & S_AIRBASE && draw_fortress_airbase) *sprs++ = sprites.tx.airbase;
  if(tspecial & S_POLLUTION && draw_pollution) *sprs++ = sprites.tx.pollution;
  if(tspecial & S_FALLOUT && draw_pollution) *sprs++ = sprites.tx.fallout;
  if(ptile->known==TILE_KNOWN_FOGGED && draw_fog_of_war) *sprs++ = sprites.tx.fog;

  if(!citymode) {
    tileno = INDEX_NSEW((tile_is_known(abs_x0, abs_y0-1)==TILE_UNKNOWN),
                        (tile_is_known(abs_x0, abs_y0+1)==TILE_UNKNOWN),
                        (tile_is_known(abs_x0+1, abs_y0)==TILE_UNKNOWN),
                        (tile_is_known(abs_x0-1, abs_y0)==TILE_UNKNOWN));
    if (tileno) 
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
	  no_backdrop=BOOL_VAL(pcity);
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
	assert(sp_wall);
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

  if( !city_styles[style].tiles_num ) {  /* no tiles found, try alternate */

    freelog(LOG_NORMAL, "No tiles for %s style, trying alternate %s style",
            city_styles[style].graphic, city_styles[style].graphic_alt);

    tilespec_setup_style_tile(style, city_styles[style].graphic_alt);
  }

  if( !city_styles[style].tiles_num ) {  /* no alternate, use default */

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
    if (is_isometric)
      free(sprites.city.tile_wall[i]);
    free(sprites.city.tile[i]);
  }

  if (is_isometric)
    free(sprites.city.tile_wall);
  free(sprites.city.tile);
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

  if(!ptile->known) {
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
  } else if(ptile->terrain==T_OCEAN) {
    color=COLOR_STD_OCEAN;
  } else {
    color=COLOR_STD_GROUND;
  }

  return color;
}

/**********************************************************************
  Set focus_unit_hidden (q.v.) variable to given value.
***********************************************************************/
void set_focus_unit_hidden_state(int hide)
{
  focus_unit_hidden = hide;
}

/**********************************************************************
...
***********************************************************************/
struct unit *get_drawable_unit(int x, int y, int citymode)
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
