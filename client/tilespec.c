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

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "astring.h"
#include "capability.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "player.h"
#include "registry.h"
#include "shared.h"
#include "unit.h"

#include "graphics_g.h"

#include "tilespec.h"

extern struct player_race *races;

char *main_intro_filename;
char *minimap_intro_filename;

struct named_sprites sprites;

static int num_spec_files = 0;
static char **spec_filenames;	/* full pathnames */

/* The list of sprites (as used to be in graphics.c)
   Plus an athing to keep track of currently allocated size.
   (reallocs are ok, because the pointers we keep are
   the things stored by tile_sprites, not pointers into
   the tile_sprites array itself).
*/
static struct Sprite **tile_sprites;
static struct athing atile_sprites;   /* .ptr is tile_sprites */

/* Use an internal section_file to store the tiles index numbers
   corresponding to each tag.  Use section_file purely to get
   hashing functionality.  (Should really have separate hashing
   stuff, and use that, but this works for now.)

   This is kept permanently after setup, for doing lookups
   on ruleset information (including on reconnections etc).

   The entries are simply the tags from the tile spec files,
   with integer values corresponding to index in tile_sprites.
*/
static struct section_file tag_sf;

/***************************************************************************
  Crop sprite for source and store in gloabl tile_sprites.
  Reallocates tile_sprites as required, using an athing to
  keep track.  (Note this is not as bad as it sounds: we
  are only reallocing the pointers to sprites, not the sprites
  themselves.)
  Returns index to this sprite in tile_sprites.
***************************************************************************/
static int crop_sprite_to_tiles(struct Sprite *source,
				int x, int y, int dx, int dy)
{
  assert(source);
  
  if (tile_sprites==NULL) {
    ath_init(&atile_sprites, sizeof(struct Sprite*));
  }
  ath_minnum(&atile_sprites, atile_sprites.n+1);
  tile_sprites = atile_sprites.ptr;
  tile_sprites[atile_sprites.n-1] = crop_sprite(source, x, y, dx, dy);
  return atile_sprites.n-1;
}

/**********************************************************************
  Reallocs tile_sprites exactly to current size;
  Do this at the end to avoid "permanent" waste.
***********************************************************************/
static void final_realloc_tile_sprites(void)
{
  atile_sprites.ptr = fc_realloc(atile_sprites.ptr,
				 atile_sprites.size*atile_sprites.n);
  atile_sprites.n_alloc = atile_sprites.n;
  tile_sprites = atile_sprites.ptr;
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
  char *tileset_default = "default";
  char *fname, *dname;
  int level;

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

  if (strcmp(tileset_name, tileset_default)==0) {
    level = LOG_FATAL;
  } else {
    level = LOG_NORMAL;
  }
  freelog(level, "Could not find readable file \"%s\" in data path",
	  fname);
  freelog(level, "The data path may be set via"
	  " the environment variable FREECIV_PATH");
  freelog(level, "Current data path is: \"%s\"", datafilename(NULL));
  if (level == LOG_FATAL) {
    exit(1);
  }
  freelog(level, "Trying \"%s\" tileset", tileset_default);
  free(fname);
  return tilespec_fullname(tileset_default);
}

/**********************************************************************
  Checks options in filename match what we require and support.
  Die if not.
  which should be "tilespec" or "spec".
***********************************************************************/
static void check_tilespec_capabilities(struct section_file *file,
					const char *which,
					const char *us_capstr,
					const char *filename)
{
  char *file_capstr = secfile_lookup_str(file, "%s.options", which);
  
  if (!has_capabilities(us_capstr, file_capstr)) {
    freelog(LOG_FATAL, "%s file appears incompatible", which);
    freelog(LOG_FATAL, "file: \"%s\"", filename);
    freelog(LOG_FATAL, "file options: %s", file_capstr);
    freelog(LOG_FATAL, "supported options: %s", us_capstr);
    exit(1);
  }
  if (!has_capabilities(file_capstr, us_capstr)) {
    freelog(LOG_FATAL,
	    "%s file claims required option(s) which we don't support", which);
    freelog(LOG_FATAL, "file: \"%s\"", filename);
    freelog(LOG_FATAL, "file options: %s", file_capstr);
    freelog(LOG_FATAL, "supported options: %s", us_capstr);
    exit(1);
  }
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
    freelog(LOG_FATAL, "Could not open \"%s\"", fname);
    exit(1);
  }
  check_tilespec_capabilities(file, "tilespec", "+tilespec1", fname);

  section_file_lookup(file, "tilespec.name"); /* currently unused */

  NORMAL_TILE_WIDTH = secfile_lookup_int(file, "tilespec.normal_tile_width");
  NORMAL_TILE_HEIGHT = secfile_lookup_int(file, "tilespec.normal_tile_height");
  SMALL_TILE_WIDTH = secfile_lookup_int(file, "tilespec.small_tile_width");
  SMALL_TILE_HEIGHT = secfile_lookup_int(file, "tilespec.small_tile_height");
  freelog(LOG_VERBOSE, "tile sizes %dx%d, %dx%d small",
	  NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT,
	  SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT);

  /* TODO: use this */
  section_file_lookup(file, "tilespec.flags_are_transparent"); 

  c = secfile_lookup_str(file, "tilespec.main_intro_file");
  main_intro_filename = mystrdup(datafilename_required(c));
  freelog(LOG_DEBUG, "intro file %s", main_intro_filename);
  
  c = secfile_lookup_str(file, "tilespec.minimap_intro_file");
  minimap_intro_filename = mystrdup(datafilename_required(c));
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
  and save indices in tag_sf.
***********************************************************************/
static void tilespec_load_one(const char *spec_filename)
{
  struct section_file the_file, *file = &the_file;
  struct Sprite *big_sprite;
  char *xpm_filename;
  char **gridnames, **tags;
  int num_grids, num_tags;
  int i, j, k;
  int x_top_left, y_top_left, dx, dy;
  int row, column;
  int idx, x1, y1;

  freelog(LOG_DEBUG, "loading spec %s", spec_filename);

  freelog(LOG_DEBUG, "loading spec %s", spec_filename);
  if (!section_file_load(file, spec_filename)) {
    freelog(LOG_FATAL, "Could not open \"%s\"", spec_filename);
    exit(1);
  }
  check_tilespec_capabilities(file, "spec", "+spec1", spec_filename);

  section_file_lookup(file, "info.artists"); /* currently unused */

  xpm_filename = datafilename_required(secfile_lookup_str(file, "file.xpm"));
  freelog(LOG_DEBUG, "loading xpm %s", xpm_filename);
  big_sprite = load_xpmfile(xpm_filename);

  gridnames = secfile_get_secnames_prefix(file, "grid_", &num_grids);
  if (num_grids==0) {
    freelog(LOG_FATAL, "spec %s has no grid_* sections", spec_filename);
    exit(1);
  }

  for(i=0; i<num_grids; i++) {
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

      x1 = x_top_left + column * dx;
      y1 = y_top_left + row * dy;
      idx = crop_sprite_to_tiles(big_sprite, x1, y1, dx, dy);

      for(k=0; k<num_tags; k++) {
	secfile_insert_int(&tag_sf, idx, "%s", tags[k]);
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
  static char c[] = "n0s0e0w0";

  sprintf(c, "n%ds%de%dw%d", !!(idx&BIT_NORTH), !!(idx&BIT_SOUTH),
     	                     !!(idx&BIT_EAST), !!(idx&BIT_WEST));
  return c;
}
     
/* Note very safe, but convenient: */
#define SET_SPRITE(field, tag) \
    sprites.field = tile_sprites[secfile_lookup_int(&tag_sf, "%s", (tag))]

/**********************************************************************
  Initialize 'sprites' structure based on hardwired tags which
  freeciv always requires. 
***********************************************************************/
static void tilespec_lookup_sprite_tags(void)
{
  char buffer[512];
  char dir_char[] = "nsew";
  int i, j;
  
  assert(secfilehash_hashash(&tag_sf));

  SET_SPRITE(treaty_thumb[0], "treaty.disagree_thumb_down");
  SET_SPRITE(treaty_thumb[1], "treaty.agree_thumb_up");

  for(i=0; i<NUM_TILES_PROGRESS; i++) {
    sprintf(buffer, "s.science_bulb_%d", i);
    SET_SPRITE(bulb[i], buffer);
    sprintf(buffer, "s.warming_sun_%d", i);
    SET_SPRITE(warming[i], buffer);
  }

  SET_SPRITE(right_arrow, "s.right_arrow");

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
  SET_SPRITE(spaceship.life_support, "spaceship.solar_panels");
  SET_SPRITE(spaceship.habitation,   "spaceship.habitation");
  SET_SPRITE(spaceship.structural,   "spaceship.structural");
  SET_SPRITE(spaceship.fuel,         "spaceship.fuel");
  SET_SPRITE(spaceship.propulsion,   "spaceship.propulsion");

  SET_SPRITE(road.isolated, "r.road_isolated");
  SET_SPRITE(rail.isolated, "r.rail_isolated");
  
  for(i=1; i<NUM_DIRECTION_NSEW; i++) {
    sprintf(buffer, "r.c_road_%s", nsew_str(i));
    SET_SPRITE(road.cardinal[i], buffer);
    
    sprintf(buffer, "r.d_road_%s", nsew_str(i));
    SET_SPRITE(road.diagonal[i], buffer);
    
    sprintf(buffer, "r.c_rail_%s", nsew_str(i));
    SET_SPRITE(rail.cardinal[i], buffer);
    
    sprintf(buffer, "r.d_rail_%s", nsew_str(i));
    SET_SPRITE(rail.diagonal[i], buffer);
  }

  for(i=0; i<3; i++) {
    for(j=0; j<3; j++) {
      sprintf(buffer, "explode.nuke_%d%d", i, j);
      SET_SPRITE(explode.nuke[i][j], buffer);
    }
  }

  SET_SPRITE(unit.auto_attack,  "unit.auto_attack");
  SET_SPRITE(unit.auto_settler, "unit.auto_settler");
  SET_SPRITE(unit.auto_explore, "unit.auto_explore");
  SET_SPRITE(unit.fortify,	"unit.fortify");     
  SET_SPRITE(unit.fortress,     "unit.fortress");
  SET_SPRITE(unit.go_to,	"unit.goto");     
  SET_SPRITE(unit.irrigate,     "unit.irrigate");
  SET_SPRITE(unit.mine,	        "unit.mine");
  SET_SPRITE(unit.pillage,	"unit.pillage");
  SET_SPRITE(unit.pollution,    "unit.pollution");
  SET_SPRITE(unit.road,	        "unit.road");
  SET_SPRITE(unit.sentry,	"unit.sentry");      
  SET_SPRITE(unit.stack,	"unit.stack");
  SET_SPRITE(unit.transform,    "unit.transform");   

  for(i=0; i<NUM_TILES_HP_BAR; i++) {
    sprintf(buffer, "unit.hp_%d", i*10);
    SET_SPRITE(unit.hp_bar[i], buffer);
  }

  SET_SPRITE(city.occupied, "city.occupied");
  SET_SPRITE(city.disorder, "city.disorder");

  for(i=0; i<NUM_TILES_DIGITS; i++) {
    sprintf(buffer, "city.size_%d", i);
    SET_SPRITE(city.size[i], buffer);
    if(i!=0) {
      sprintf(buffer, "city.size_%d", i*10);
      SET_SPRITE(city.size_tens[i], buffer);
    }
    sprintf(buffer, "city.t_food_%d", i);
    SET_SPRITE(city.tile_foodnum[i], buffer);
    sprintf(buffer, "city.t_shields_%d", i);
    SET_SPRITE(city.tile_shieldnum[i], buffer);
    sprintf(buffer, "city.t_trade_%d", i);
    SET_SPRITE(city.tile_tradenum[i], buffer);
  }

  SET_SPRITE(upkeep.food[0], "upkeep.food");
  SET_SPRITE(upkeep.food[1], "upkeep.food2");
  SET_SPRITE(upkeep.unhappy[0], "upkeep.unhappy");
  SET_SPRITE(upkeep.unhappy[1], "upkeep.unhappy2");
  SET_SPRITE(upkeep.shield, "upkeep.shield");
  
  SET_SPRITE(user.attention, "user.attention");

  SET_SPRITE(tx.farmland,   "tx.farmland");
  SET_SPRITE(tx.irrigation, "tx.irrigation");
  SET_SPRITE(tx.mine,       "tx.mine");
  SET_SPRITE(tx.oil_mine,   "tx.oil_mine");
  SET_SPRITE(tx.pollution,  "tx.pollution");
  SET_SPRITE(tx.city,       "tx.city");
  SET_SPRITE(tx.city_walls, "tx.city_walls");
  SET_SPRITE(tx.village,    "tx.village");
  SET_SPRITE(tx.fortress,   "tx.fortress");

  for(i=0; i<NUM_DIRECTION_NSEW; i++) {
    sprintf(buffer, "tx.s_river_%s", nsew_str(i));
    SET_SPRITE(tx.spec_river[i], buffer);
  }
  for(i=1; i<NUM_DIRECTION_NSEW; i++) {
    sprintf(buffer, "tx.darkness_%s", nsew_str(i));
    SET_SPRITE(tx.darkness[i], buffer);
    sprintf(buffer, "tx.coast_cape_%s", nsew_str(i));
    SET_SPRITE(tx.coast_cape[i], buffer);
  }
  for(i=0; i<4; i++) {
    sprintf(buffer, "tx.river_outlet_%c", dir_char[i]);
    SET_SPRITE(tx.river_outlet[i], buffer);
  }
  for(i=0; i<2; i++) {
    for(j=0; j<3; j++) {
      sprintf(buffer, "tx.denmark_%d%d", i, j);
      SET_SPRITE(tx.denmark[i][j], buffer);
    }
  }
}

/**********************************************************************
  Load the tiles; requires tilespec_read_toplevel() called previously.
  Leads to tile_sprites being allocated and filled with pointers
  to sprites.   Also sets up and populates tag_sf, and calls func
  to initialize 'sprites' structure.
***********************************************************************/
void tilespec_load_tiles(void)
{
  int i;
  
  assert(num_spec_files>0);
  section_file_init(&tag_sf);

  for(i=0; i<num_spec_files; i++) {
    tilespec_load_one(spec_filenames[i]);
  }
  final_realloc_tile_sprites();

  secfilehash_build(&tag_sf);
  /* section_file_save(&tag_sf, "_debug_tilespec"); */

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
  int loglevel = required ? LOG_NORMAL : LOG_DEBUG;
  int i;
  
  /* (should get tag_sf before connection) */
  if (!secfilehash_hashash(&tag_sf)) {
    freelog(LOG_FATAL, "attempt to lookup for %s %s before tag_sf setup",
	    what, name);
    exit(1);
  }

  /* real values are always non-negative: */
  i = secfile_lookup_int_default(&tag_sf, -1, "%s", tag);
  if (i != -1) {
    assert(i>=0 && i<atile_sprites.n);
    return tile_sprites[i];
  }
  
  i = secfile_lookup_int_default(&tag_sf, -1, "%s", alt);
  if (i != -1) {
    assert(i>=0 && i<atile_sprites.n);
    freelog(loglevel, "Using alternate graphic %s (instead of %s) for %s %s",
	    alt, tag, what, name);
    return tile_sprites[i];
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
  char *nsew;
  char buffer1[MAX_LEN_NAME+20], buffer2[MAX_LEN_NAME+20];
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
  for(i=0; i<NUM_DIRECTION_NSEW; i++) {
    nsew = nsew_str(i);
    sprintf(buffer1, "%s_%s", tt->graphic_str, nsew);
    sprintf(buffer2, "%s_%s", tt->graphic_alt, nsew);
    
    tt->sprite[i] = lookup_sprite_tag_alt(buffer1, buffer2, 1, "tile_type",
					  tt->terrain_name);
    
    /* should probably do something if NULL, eg generic default? */
  }
  for(i=0; i<2; i++) {
    char *name = i ? tt->special_2_name : tt->special_1_name;
    tt->special[i].sprite
      = lookup_sprite_tag_alt(tt->special[i].graphic_str,
			      tt->special[i].graphic_alt,
			      name[0], "tile_type special", name);
    
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
  struct player_race *this = &races[id];

  this->flag_sprite = lookup_sprite_tag_alt(this->flag_graphic_str, 
					    this->flag_graphic_alt,
					    1, "nation", this->name);

  /* should probably do something if NULL, eg generic default? */
}
