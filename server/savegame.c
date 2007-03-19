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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "registry.h"
#include "shared.h"
#include "support.h"

#include "capability.h"
#include "city.h"
#include "game.h"
#include "government.h"
#include "idex.h"
#include "map.h"
#include "movement.h"
#include "unit.h"
#include "unitlist.h"
#include "version.h"

#include "aicity.h"
#include "aidata.h"
#include "aiunit.h"

#include "script.h"

#include "barbarian.h"
#include "citytools.h"
#include "cityturn.h"
#include "diplhand.h"
#include "mapgen.h"
#include "maphand.h"
#include "meta.h"
#include "plrhand.h"
#include "ruleset.h"
#include "savegame.h"
#include "score.h"
#include "spacerace.h"
#include "srv_main.h"
#include "stdinhand.h"
#include "techtools.h"
#include "unittools.h"

#define TOKEN_SIZE 10

/* 
 * This loops over the entire map to save data. It collects all the data of
 * a line using GET_XY_CHAR and then executes the macro SECFILE_INSERT_LINE.
 * Internal variables map_x, map_y, nat_x, nat_y, and line are allocated
 * within the macro but definable by the caller of the macro.
 *
 * Parameters:
 *   line: buffer variable to hold a line of chars
 *   map_x, map_y: variables for internal map coordinates
 *   nat_x, nat_y: variables for output/native coordinates
 *   GET_XY_CHAR: macro returning the map character for each position
 *   SECFILE_INSERT_LINE: macro to output each processed line (line nat_y)
 *
 * Note: don't use this macro DIRECTLY, use 
 * SAVE_NORMAL_MAP_DATA or SAVE_PLAYER_MAP_DATA instead.
 */
#define SAVE_MAP_DATA(ptile, line,					    \
                      GET_XY_CHAR, SECFILE_INSERT_LINE)                     \
{                                                                           \
  char line[map.xsize + 1];                                                 \
  int _nat_x, _nat_y;							    \
                                                                            \
  for (_nat_y = 0; _nat_y < map.ysize; _nat_y++) {			    \
    for (_nat_x = 0; _nat_x < map.xsize; _nat_x++) {			    \
      struct tile *ptile = native_pos_to_tile(_nat_x, _nat_y);		          \
      assert(ptile != NULL);                                                \
      line[_nat_x] = (GET_XY_CHAR);                                         \
      if (!my_isprint(line[_nat_x] & 0x7f)) {                               \
          die("Trying to write invalid map "                                \
              "data: '%c' %d", line[_nat_x], line[_nat_x]);                 \
      }                                                                     \
    }                                                                       \
    line[map.xsize] = '\0';                                                 \
    (SECFILE_INSERT_LINE);                                                  \
  }                                                                         \
}

/*
 * Wrappers for SAVE_MAP_DATA.
 *
 * SAVE_NORMAL_MAP_DATA saves a standard line of map data.
 *
 * SAVE_PLAYER_MAP_DATA saves a line of map data from a playermap.
 */
#define SAVE_NORMAL_MAP_DATA(ptile, secfile, secname, GET_XY_CHAR)	    \
  SAVE_MAP_DATA(ptile, _line, GET_XY_CHAR,				    \
		secfile_insert_str(secfile, _line, secname, _nat_y))

#define SAVE_PLAYER_MAP_DATA(ptile, secfile, secname, plrno,		    \
			     GET_XY_CHAR)                                   \
  SAVE_MAP_DATA(ptile, _line, GET_XY_CHAR,				    \
		secfile_insert_str(secfile, _line, secname, plrno, _nat_y))

/*
 * This loops over the entire map to load data. It inputs a line of data
 * using the macro SECFILE_LOOKUP_LINE and then loops using the macro
 * SET_XY_CHAR to load each char into the map at (map_x, map_y).  Internal
 * variables ch, map_x, map_y, nat_x, and nat_y are allocated within the
 * macro but definable by the caller.
 *
 * Parameters:
 *   ch: a variable to hold a char (data for a single position)
 *   map_x, map_y: variables for internal map coordinates
 *   nat_x, nat_y: variables for output/native coordinates
 *   SET_XY_CHAR: macro to load the map character at each (map_x, map_y)
 *   SECFILE_LOOKUP_LINE: macro to input the nat_y line for processing
 *
 * Note: some (but not all) of the code this is replacing used to
 * skip over lines that did not exist.  This allowed for
 * backward-compatibility.  We could add another parameter that
 * specified whether it was OK to skip the data, but there's not
 * really much advantage to exiting early in this case.  Instead,
 * we let any map data type to be empty, and just print an
 * informative warning message about it.
 */
#define LOAD_MAP_DATA(ch, nat_y, ptile,					    \
		      SECFILE_LOOKUP_LINE, SET_XY_CHAR)                     \
{                                                                           \
  int _nat_x, _nat_y;							    \
                                                                            \
  bool _warning_printed = FALSE;                                            \
  for (_nat_y = 0; _nat_y < map.ysize; _nat_y++) {			    \
    const int nat_y = _nat_y;						    \
    const char *_line = (SECFILE_LOOKUP_LINE);                              \
                                                                            \
    if (!_line || strlen(_line) != map.xsize) {                             \
      if (!_warning_printed) {                                              \
        /* TRANS: Error message. */                                         \
        freelog(LOG_ERROR, _("The save file contains incomplete "           \
                "map data.  This can happen with old saved "                \
                "games, or it may indicate an invalid saved "               \
                "game file.  Proceed at your own risk."));                  \
        if(!_line) {                                                        \
          /* TRANS: Error message. */                                       \
          freelog(LOG_ERROR, _("Reason: line not found"));                  \
        } else {                                                            \
          /* TRANS: Error message. */                                       \
          freelog(LOG_ERROR, _("Reason: line too short "                    \
                  "(expected %d got %lu"), map.xsize,                       \
                  (unsigned long) strlen(_line));                           \
        }                                                                   \
        /* Do not translate.. */                                            \
        freelog(LOG_ERROR, "secfile_lookup_line='%s'",                      \
                #SECFILE_LOOKUP_LINE);                                      \
        _warning_printed = TRUE;                                            \
      }                                                                     \
      continue;                                                             \
    }                                                                       \
    for (_nat_x = 0; _nat_x < map.xsize; _nat_x++) {			    \
      const char ch = _line[_nat_x];                                        \
      struct tile *ptile = native_pos_to_tile(_nat_x, _nat_y);		    \
                                                                            \
      (SET_XY_CHAR);                                                        \
    }                                                                       \
  }                                                                         \
}

/* Iterate on the specials half-bytes */
#define special_halfbyte_iterate(s)					    \
{									    \
  enum tile_special_type s;						    \
									    \
  for(s = 0; 4 * s < S_LAST; s++) {

#define special_halfbyte_iterate_end					    \
  }									    \
}

/* The following should be removed when compatibility with
   pre-1.13.0 savegames is broken: startoptions, spacerace2
   and rulesets */
#define SAVEFILE_OPTIONS "startoptions spacerace2 rulesets" \
" diplchance_percent map_editor known32fix turn " \
"attributes watchtower rulesetdir client_worklists orders " \
"startunits turn_last_built improvement_order technology_order embassies " \
"new_owner_map resources"

static const char hex_chars[] = "0123456789abcdef";

static void set_savegame_special(bv_special *specials,
		    char ch, const enum tile_special_type *index);

/***************************************************************
This returns an ascii hex value of the given half-byte of the binary
integer. See ascii_hex2bin().
  example: bin2ascii_hex(0xa00, 2) == 'a'
***************************************************************/
#define bin2ascii_hex(value, halfbyte_wanted) \
  hex_chars[((value) >> ((halfbyte_wanted) * 4)) & 0xf]

/***************************************************************
This returns a binary integer value of the ascii hex char, offset by
the given number of half-bytes. See bin2ascii_hex().
  example: ascii_hex2bin('a', 2) == 0xa00
This is only used in loading games, and it requires some error
checking so it's done as a function.
***************************************************************/
static int ascii_hex2bin(char ch, int halfbyte)
{
  char *pch;

  if (ch == ' ') {
    /* 
     * Sane value. It is unknow if there are savegames out there which
     * need this fix. Savegame.c doesn't write such savegames
     * (anymore) since the inclusion into CVS (2000-08-25).
     */
    return 0;
  }
  
  pch = strchr(hex_chars, ch);

  if (!pch || ch == '\0') {
    die("Unknown hex value: '%c' %d", ch, ch);
  }
  return (pch - hex_chars) << (halfbyte * 4);
}

/****************************************************************************
  Dereferences the terrain character.  See terrains[].identifier
    example: char2terrain('a') => T_ARCTIC
****************************************************************************/
static struct terrain *char2terrain(char ch)
{
  if (ch == UNKNOWN_TERRAIN_IDENTIFIER) {
    return T_UNKNOWN;
  }
  terrain_type_iterate(pterrain) {
    if (pterrain->identifier == ch) {
      return pterrain;
    }
  } terrain_type_iterate_end;

  /* TRANS: message for an obscure savegame error. */
  freelog(LOG_FATAL, _("Unknown terrain identifier '%c' in savegame."), ch);
  exit(EXIT_FAILURE);
}

/****************************************************************************
  References the terrain character.  See terrains[].identifier
    example: terrain2char(T_ARCTIC) => 'a'
****************************************************************************/
static char terrain2char(const struct terrain *pterrain)
{
  if (pterrain == T_UNKNOWN) {
    return UNKNOWN_TERRAIN_IDENTIFIER;
  } else {
    return pterrain->identifier;
  }
}

/****************************************************************************
  Returns an order for a character identifier.  See also order2char.
****************************************************************************/
static enum unit_orders char2order(char order)
{
  switch (order) {
  case 'm':
  case 'M':
    return ORDER_MOVE;
  case 'w':
  case 'W':
    return ORDER_FULL_MP;
  case 'b':
  case 'B':
    return ORDER_BUILD_CITY;
  case 'a':
  case 'A':
    return ORDER_ACTIVITY;
  case 'd':
  case 'D':
    return ORDER_DISBAND;
  case 'u':
  case 'U':
    return ORDER_BUILD_WONDER;
  case 't':
  case 'T':
    return ORDER_TRADEROUTE;
  case 'h':
  case 'H':
    return ORDER_HOMECITY;
  }

  /* This can happen if the savegame is invalid. */
  return ORDER_LAST;
}

/****************************************************************************
  Returns a character identifier for an order.  See also char2order.
****************************************************************************/
static char order2char(enum unit_orders order)
{
  switch (order) {
  case ORDER_MOVE:
    return 'm';
  case ORDER_FULL_MP:
    return 'w';
  case ORDER_ACTIVITY:
    return 'a';
  case ORDER_BUILD_CITY:
    return 'b';
  case ORDER_DISBAND:
    return 'd';
  case ORDER_BUILD_WONDER:
    return 'u';
  case ORDER_TRADEROUTE:
    return 't';
  case ORDER_HOMECITY:
    return 'h';
  case ORDER_LAST:
    break;
  }

  assert(0);
  return '?';
}

/****************************************************************************
  Returns a direction for a character identifier.  See also dir2char.
****************************************************************************/
static enum direction8 char2dir(char dir)
{
  /* Numberpad values for the directions. */
  switch (dir) {
  case '1':
    return DIR8_SOUTHWEST;
  case '2':
    return DIR8_SOUTH;
  case '3':
    return DIR8_SOUTHEAST;
  case '4':
    return DIR8_WEST;
  case '6':
    return DIR8_EAST;
  case '7':
    return DIR8_NORTHWEST;
  case '8':
    return DIR8_NORTH;
  case '9':
    return DIR8_NORTHEAST;
  }

  /* This can happen if the savegame is invalid. */
  return DIR8_LAST;
}

/****************************************************************************
  Returns a character identifier for a direction.  See also char2dir.
****************************************************************************/
static char dir2char(enum direction8 dir)
{
  /* Numberpad values for the directions. */
  switch (dir) {
  case DIR8_NORTH:
    return '8';
  case DIR8_SOUTH:
    return '2';
  case DIR8_EAST:
    return '6';
  case DIR8_WEST:
    return '4';
  case DIR8_NORTHEAST:
    return '9';
  case DIR8_NORTHWEST:
    return '7';
  case DIR8_SOUTHEAST:
    return '3';
  case DIR8_SOUTHWEST:
    return '1';
  }

  assert(0);
  return '?';
}

/****************************************************************************
  Returns a character identifier for an activity.  See also char2activity.
****************************************************************************/
static char activity2char(enum unit_activity activity)
{
  switch (activity) {
  case ACTIVITY_IDLE:
    return 'w';
  case ACTIVITY_POLLUTION:
    return 'p';
  case ACTIVITY_ROAD:
    return 'r';
  case ACTIVITY_MINE:
    return 'm';
  case ACTIVITY_IRRIGATE:
    return 'i';
  case ACTIVITY_FORTIFIED:
    return 'f';
  case ACTIVITY_FORTRESS:
    return 't';
  case ACTIVITY_SENTRY:
    return 's';
  case ACTIVITY_RAILROAD:
    return 'l';
  case ACTIVITY_PILLAGE:
    return 'e';
  case ACTIVITY_GOTO:
    return 'g';
  case ACTIVITY_EXPLORE:
    return 'x';
  case ACTIVITY_TRANSFORM:
    return 'o';
  case ACTIVITY_AIRBASE:
    return 'a';
  case ACTIVITY_FORTIFYING:
    return 'y';
  case ACTIVITY_FALLOUT:
    return 'u';
  case ACTIVITY_BASE:
    return 'b';
  case ACTIVITY_UNKNOWN:
  case ACTIVITY_PATROL_UNUSED:
    return '?';
  case ACTIVITY_LAST:
    break;
  }

  assert(0);
  return '?';
}

/****************************************************************************
  Returns an activity for a character identifier.  See also activity2char.
****************************************************************************/
static enum unit_activity char2activity(char activity)
{
  enum unit_activity a;

  for (a = 0; a < ACTIVITY_LAST; a++) {
    char achar = activity2char(a);

    if (activity == achar || activity == my_toupper(achar)) {
      return a;
    }
  }

  /* This can happen if the savegame is invalid. */
  return ACTIVITY_LAST;
}

/***************************************************************
Quote the memory block denoted by data and length so it consists only
of " a-f0-9:". The returned string has to be freed by the caller using
free().
***************************************************************/
static char *quote_block(const void *const data, int length)
{
  char *buffer = fc_malloc(length * 3 + 10);
  size_t offset;
  int i;

  sprintf(buffer, "%d:", length);
  offset = strlen(buffer);

  for (i = 0; i < length; i++) {
    sprintf(buffer + offset, "%02x ", ((unsigned char *) data)[i]);
    offset += 3;
  }
  return buffer;
}

/***************************************************************
Unquote a string. The unquoted data is written into dest. If the
unqoted data will be largern than dest_length the function aborts. It
returns the actual length of the unquoted block.
***************************************************************/
static int unquote_block(const char *const quoted_, void *dest,
			 int dest_length)
{
  int i, length, parsed, tmp;
  char *endptr;
  const char *quoted = quoted_;

  parsed = sscanf(quoted, "%d", &length);
  assert(parsed == 1);

  assert(length <= dest_length);
  quoted = strchr(quoted, ':');
  assert(quoted != NULL);
  quoted++;

  for (i = 0; i < length; i++) {
    tmp = strtol(quoted, &endptr, 16);
    assert((endptr - quoted) == 2);
    assert(*endptr == ' ');
    assert((tmp & 0xff) == tmp);
    ((unsigned char *) dest)[i] = tmp;
    quoted += 3;
  }
  return length;
}

/***************************************************************
load starting positions for the players from a savegame file
Now we don't know how many start positions there are nor how many
should be because rulesets are loaded later. So try to load as
many as they are; there should be at least enough for every
player.  This could be changed/improved in future.
***************************************************************/
static void map_startpos_load(struct section_file *file)
{
  int savegame_start_positions;
  int i, j;
  int nat_x, nat_y;
  
  for (savegame_start_positions = 0;
       secfile_lookup_int_default(file, -1, "map.r%dsx",
                                  savegame_start_positions) != -1;
       savegame_start_positions++) {
    /* Nothing. */
  }
  
  
  {
    struct start_position start_positions[savegame_start_positions];
    
    for (i = j = 0; i < savegame_start_positions; i++) {
      char *nation_name = secfile_lookup_str_default(file, NULL,
						     "map.r%dsnation", i);
      struct nation_type *pnation;

      if (!nation_name) {
        /* Starting positions in normal games are saved without nation.
	   Just ignore it */
	continue;
      }

      pnation = find_nation_by_name_orig(nation_name);
      if (pnation == NO_NATION_SELECTED) {
	freelog(LOG_NORMAL,
	        _("Warning: Unknown nation %s for starting position no %d"),
		nation_name, i);
	continue;
      }
      
      nat_x = secfile_lookup_int(file, "map.r%dsx", i);
      nat_y = secfile_lookup_int(file, "map.r%dsy", i);

      start_positions[j].tile = native_pos_to_tile(nat_x, nat_y);
      start_positions[j].nation = pnation;
      j++;
    }
    map.num_start_positions = j;
    if (map.num_start_positions > 0) {
      map.start_positions = fc_realloc(map.start_positions,
  	                               map.num_start_positions
				       * sizeof(*map.start_positions));
      for (i = 0; i < j; i++) {
        map.start_positions[i] = start_positions[i];
      }
    }
  }
  

  if (map.num_start_positions
      && map.num_start_positions < game.info.max_players) {
    freelog(LOG_VERBOSE,
	    _("Number of starts (%d) are lower than rules.max_players (%d),"
	      " lowering rules.max_players."),
 	    map.num_start_positions, game.info.max_players);
    game.info.max_players = map.num_start_positions;
  }
}

/***************************************************************
load the tile map from a savegame file
***************************************************************/
static void map_tiles_load(struct section_file *file)
{
  map.topology_id = secfile_lookup_int_default(file, MAP_ORIGINAL_TOPO,
					       "map.topology_id");

  /* In some cases we read these before, but not always, and
   * its safe to read them again:
   */
  map.xsize=secfile_lookup_int(file, "map.width");
  map.ysize=secfile_lookup_int(file, "map.height");

  /* With a FALSE parameter [xy]size are not changed by this call. */
  map_init_topology(FALSE);

  map_allocate();

  /* get the terrain type */
  LOAD_MAP_DATA(ch, line, ptile,
		secfile_lookup_str(file, "map.t%03d", line),
		ptile->terrain = char2terrain(ch));

  assign_continent_numbers();

  whole_map_iterate(ptile) {
    ptile->spec_sprite = secfile_lookup_str_default(file, NULL,
				"map.spec_sprite_%d_%d",
				ptile->nat_x, ptile->nat_y);
    if (ptile->spec_sprite) {
      ptile->spec_sprite = mystrdup(ptile->spec_sprite);
    }
  } whole_map_iterate_end;
}

/* The order of the special bits, so that we can rebuild the
 * bitvector from old savegames (where the order of the bits is
 * unspecified). Here S_LAST means an unused bit (but see also resource
 * recovery). */
static const enum tile_special_type default_specials[] = {
  S_LAST, S_ROAD, S_IRRIGATION, S_RAILROAD,
  S_MINE, S_POLLUTION, S_HUT, S_FORTRESS,
  S_LAST, S_RIVER, S_FARMLAND, S_AIRBASE,
  S_FALLOUT, S_LAST, S_LAST, S_LAST
};

/****************************************************************************
  Load the rivers overlay map from a savegame file.

  A scenario may define the terrain of the map but not list the specials on
  it (thus allowing users to control the placement of specials).  However
  rivers are a special case and must be included in the map along with the
  scenario.  Thus in those cases this function should be called to load
  the river information separate from any other special data.

  This does not need to be called from map_load(), because map_load() loads
  the rivers overlay along with the rest of the specials.  Call this only
  if you've already called map_tiles_load(), and want to load only the
  rivers overlay but no other specials.  Scenarios that encode things this
  way should have the "riversoverlay" capability.
****************************************************************************/
static void map_rivers_overlay_load(struct section_file *file,
			      const enum tile_special_type *special_order)
{
  /* used by set_savegame_special */
  map.have_rivers_overlay = TRUE;

  if (special_order) {
    special_halfbyte_iterate(j) {
      char buf[16]; /* enough for sprintf() below */
      sprintf (buf, "map.spe%02d%%03d", j);

      LOAD_MAP_DATA(ch, nat_y, ptile,
	secfile_lookup_str(file, buf, nat_y),
	set_savegame_special(&ptile->special, ch, special_order + 4 * j));
    } special_halfbyte_iterate_end;
  } else {
    /* Get the bits of the special flags which contain the river special
       and extract the rivers overlay from them. */
    assert(S_LAST <= 32);
    LOAD_MAP_DATA(ch, line, ptile,
      secfile_lookup_str_default(file, NULL, "map.n%03d", line),
      set_savegame_special(&ptile->special, ch, default_specials + 8));
  }
}

/****************************************************************************
  Complicated helper function for loading specials from a savegame.

  'ch' gives the character loaded from the savegame.  Specials are packed
  in four to a character in hex notation.  'index' is a mapping of
  savegame bit -> special bit. S_LAST is used to mark unused savegame bits.
****************************************************************************/
static void set_savegame_special(bv_special *specials,
				 char ch,
				 const enum tile_special_type *index)
{
  int i, bin;
  char *pch = strchr(hex_chars, ch);

  if (!pch || ch == '\0') {
    freelog(LOG_ERROR, "Unknown hex value: '%c' %d", ch, ch);
    bin = 0;
  } else {
    bin = pch - hex_chars;
  }

  for (i = 0; i < 4; i++) {
    enum tile_special_type sp = index[i];

    if (sp >= S_LAST) {
      continue;
    }
    if (map.have_rivers_overlay && sp != S_RIVER) {
      continue;
    }
    if (bin & (1 << i)) {
      set_special(specials, sp);
    }
  }
}

/****************************************************************************
  Complicated helper function for saving specials into a savegame.

  Specials are packed in four to a character in hex notation.  'index'
  specifies which set of specials are included in this character.
****************************************************************************/
static char get_savegame_special(bv_special specials,
				 const enum tile_special_type *index)
{
  int i, bin = 0;

  for (i = 0; i < 4; i++) {
    enum tile_special_type sp = index[i];

    if (sp >= S_LAST) {
      break;
    }
    if (contains_special(specials, sp)) {
      bin |= (1 << i);
    }
  }

  return hex_chars[bin];
}
/****************************************************************************
  Complicated helper function for reading resources from a savegame.
  This reads resources saved in the specials bitvector.
  ch is the character read from the map, and n is the number of the special
  (0 for special_1, 1 for special_2).
****************************************************************************/
static void set_savegame_old_resource(const struct resource **r,
				      const struct terrain *terrain,
				      char ch, int n)
{
  assert (n == 0 || n == 1);
  /* If resource is already set to non-NULL or there is no resource found
   * in this half-byte, then abort */
  if (*r || !(ascii_hex2bin (ch, 0) & 0x1) || !terrain->resources[0]) {
    return;
  }
  /* Note that we must handle the case of special_2 set when there is
   * only one resource defined for the terrain (example, Shields on
   * Grassland terrain where both resources are identical) */
  if (n == 0 || !terrain->resources[1]) {
    *r = terrain->resources[0];
  } else {
    *r = terrain->resources[1];
  }
}

/****************************************************************************
  Return the resource for the given identifier.
****************************************************************************/
static const struct resource *identifier_to_resource(char c)
{
  if (c == RESOURCE_NULL_IDENTIFIER) {
    return NULL;
  }
  resource_type_iterate (r) {
    if (r->identifier == c) {
      return r;
    }
  } resource_type_iterate_end;
  return NULL;
}

/****************************************************************************
  Return the identifier for the given resource.
****************************************************************************/
static char resource_to_identifier(const struct resource *presource)
{
  return presource ? presource->identifier : RESOURCE_NULL_IDENTIFIER;
}

/***************************************************************
load a complete map from a savegame file
***************************************************************/
static void map_load(struct section_file *file,
		     const enum tile_special_type *special_order)
{
  char *savefile_options = secfile_lookup_str(file, "savefile.options");

  /* map_init();
   * This is already called in game_init(), and calling it
   * here stomps on map.huts etc.  --dwp
   */

  map_tiles_load(file);
  if (secfile_lookup_bool_default(file, TRUE, "game.save_starts")) {
    map_startpos_load(file);
  } else {
    map.num_start_positions = 0;
  }

  if (special_order) {
    /* First load the resources. This is straightforward. */
    LOAD_MAP_DATA(ch, nat_y, ptile,
		  secfile_lookup_str(file, "map.res%03d", nat_y),
		  ptile->resource = identifier_to_resource(ch));

    special_halfbyte_iterate(j) {
      char buf[16]; /* enough for sprintf() below */
      sprintf (buf, "map.spe%02d_%%03d", j);

      LOAD_MAP_DATA(ch, nat_y, ptile,
	  secfile_lookup_str(file, buf, nat_y),
	  set_savegame_special(&ptile->special, ch, special_order + 4 * j));
    } special_halfbyte_iterate_end;
  } else {
    /* get 4-bit segments of 16-bit "special" field. */
    LOAD_MAP_DATA(ch, nat_y, ptile,
	    secfile_lookup_str(file, "map.l%03d", nat_y),
	    set_savegame_special(&ptile->special, ch, default_specials + 0));
    LOAD_MAP_DATA(ch, nat_y, ptile,
	    secfile_lookup_str(file, "map.u%03d", nat_y),
	    set_savegame_special(&ptile->special, ch, default_specials + 4));
    LOAD_MAP_DATA(ch, nat_y, ptile,
	    secfile_lookup_str_default(file, NULL, "map.n%03d", nat_y),
	    set_savegame_special(&ptile->special, ch, default_specials + 8));
    LOAD_MAP_DATA(ch, nat_y, ptile,
	    secfile_lookup_str_default(file, NULL, "map.f%03d", nat_y),
	    set_savegame_special(&ptile->special, ch, default_specials + 12));
    /* Setup resources (from half-bytes 1 and 3 of old savegames) */
    LOAD_MAP_DATA(ch, nat_y, ptile,
	secfile_lookup_str(file, "map.l%03d", nat_y),
	set_savegame_old_resource(&ptile->resource, ptile->terrain, ch, 0));
    LOAD_MAP_DATA(ch, nat_y, ptile,
	secfile_lookup_str(file, "map.n%03d", nat_y),
	set_savegame_old_resource(&ptile->resource, ptile->terrain, ch, 1));
  }

  /* Owner and ownership source are stored as plain numbers */
  if (has_capability("new_owner_map", savefile_options)) {
    int x, y;

    for (y = 0; y < map.ysize; y++) {
      char *buffer = 
            secfile_lookup_str_default(file, NULL, "map.owner%03d", y);
      char *ptr = buffer;

      if (buffer == NULL) {
        die("Savegame corrupt - map line %d not found.", y);
      }
      for (x = 0; x < map.xsize; x++) {
        char token[TOKEN_SIZE];
        int number;
        struct tile *ptile = native_pos_to_tile(x, y);

        scanin(&ptr, ",", token, sizeof(token));
        if (token[0] == '\0') {
          die("Savegame corrupt - map size not correct.");
        }
        if (strcmp(token, "-") == 0) {
          ptile->owner = NULL;
        } else {
          if (sscanf(token, "%d", &number)) {
            ptile->owner = get_player(number);
          } else {
            die("Savegame corrupt - got map owner %s in (%d, %d).", 
                token, x, y);
          }
        }
      }
    }
    for (y = 0; y < map.ysize; y++) {
      char *buffer2 = 
            secfile_lookup_str_default(file, NULL, "map.source%03d", y);
      char *ptr2 = buffer2;

      if (buffer2 == NULL) {
        die("Savegame corrupt - map line %d not found.", y);
      }
      for (x = 0; x < map.xsize; x++) {
        char token2[TOKEN_SIZE];
        int number;
        struct tile *ptile = native_pos_to_tile(x, y);

        scanin(&ptr2, ",", token2, sizeof(token2));
        if (token2[0] == '\0') {
          die("Savegame corrupt - map size not correct.");
        }
        if (strcmp(token2, "-") == 0) {
          ptile->owner_source = NULL;
        } else {
          if (sscanf(token2, "%d", &number)) {
            ptile->owner_source = index_to_tile(number);
          } else {
            die("Savegame corrupt - got map source %s in (%d, %d).", 
                token2, x, y);
          }
        }
      }
    }
  }

  if (secfile_lookup_bool_default(file, TRUE, "game.save_known")) {
    int known[MAP_INDEX_SIZE];

    /* get 4-bit segments of the first half of the 32-bit "known" field */
    LOAD_MAP_DATA(ch, nat_y, ptile,
		  secfile_lookup_str(file, "map.a%03d", nat_y),
		  known[ptile->index] = ascii_hex2bin(ch, 0));
    LOAD_MAP_DATA(ch, nat_y, ptile,
		  secfile_lookup_str(file, "map.b%03d", nat_y),
		  known[ptile->index] |= ascii_hex2bin(ch, 1));
    LOAD_MAP_DATA(ch, nat_y, ptile,
		  secfile_lookup_str(file, "map.c%03d", nat_y),
		  known[ptile->index] |= ascii_hex2bin(ch, 2));
    LOAD_MAP_DATA(ch, nat_y, ptile,
		  secfile_lookup_str(file, "map.d%03d", nat_y),
		  known[ptile->index] |= ascii_hex2bin(ch, 3));

    if (has_capability("known32fix", savefile_options)) {
      /* get 4-bit segments of the second half of the 32-bit "known" field */
      LOAD_MAP_DATA(ch, nat_y, ptile,
		    secfile_lookup_str(file, "map.e%03d", nat_y),
		    known[ptile->index] |= ascii_hex2bin(ch, 4));
      LOAD_MAP_DATA(ch, nat_y, ptile,
		    secfile_lookup_str(file, "map.g%03d", nat_y),
		    known[ptile->index] |= ascii_hex2bin(ch, 5));
      LOAD_MAP_DATA(ch, nat_y, ptile,
		    secfile_lookup_str(file, "map.h%03d", nat_y),
		    known[ptile->index] |= ascii_hex2bin(ch, 6));
      LOAD_MAP_DATA(ch, nat_y, ptile,
		    secfile_lookup_str(file, "map.i%03d", nat_y),
		    known[ptile->index] |= ascii_hex2bin(ch, 7));
    }

    /* HACK: we read the known data from hex into a 32-bit integer, and
     * now we convert it to bv_player. */
    whole_map_iterate(ptile) {
      BV_CLR_ALL(ptile->tile_known);
      players_iterate(pplayer) {
	if (known[ptile->index] & (1u << pplayer->player_no)) {
	  BV_SET(ptile->tile_known, pplayer->player_no);
	}
      } players_iterate_end;
    } whole_map_iterate_end;
  }


  map.have_resources = TRUE;
}

/***************************************************************
...
***************************************************************/
static void map_save(struct section_file *file)
{
  int i;

  /* map.xsize and map.ysize (saved as map.width and map.height)
   * are now always saved in game_save()
   */

  /* Old freecivs expect map.is_earth to be present in the savegame. */
  secfile_insert_bool(file, FALSE, "map.is_earth");

  secfile_insert_bool(file, game.save_options.save_starts, "game.save_starts");
  if (game.save_options.save_starts) {
    for (i=0; i<map.num_start_positions; i++) {
      struct tile *ptile = map.start_positions[i].tile;

      secfile_insert_int(file, ptile->nat_x, "map.r%dsx", i);
      secfile_insert_int(file, ptile->nat_y, "map.r%dsy", i);

      if (map.start_positions[i].nation != NO_NATION_SELECTED) {
	const char *nation = 
	  get_nation_name_orig(map.start_positions[i].nation);

	secfile_insert_str(file, nation, "map.r%dsnation", i);
      }
    }
  }

  whole_map_iterate(ptile) {
    if (ptile->spec_sprite) {
      secfile_insert_str(file, ptile->spec_sprite, "map.spec_sprite_%d_%d",
			 ptile->nat_x, ptile->nat_y);
    }
  } whole_map_iterate_end;
    
  /* put the terrain type */
  SAVE_NORMAL_MAP_DATA(ptile, file, "map.t%03d",
		       terrain2char(ptile->terrain));

  if (!map.have_resources) {
    if (map.have_rivers_overlay) {
      /* 
       * Save the rivers overlay map; this is a special case to allow
       * re-saving scenarios which have rivers overlay data.  This only
       * applies if don't have rest of specials.
       */
      special_halfbyte_iterate(j) {
	char buf[16]; /* enough for sprintf() below */
	enum tile_special_type mod[4];
	int l;

	for (l = 0; l < 4; l++) {
	  mod[l] = (4 * j + l == S_RIVER) ? S_RIVER : S_LAST;
	}
	sprintf (buf, "map.spe%02d_%%03d", j);
	SAVE_NORMAL_MAP_DATA(ptile, file, buf,
	    get_savegame_special(ptile->special, mod));
      } special_halfbyte_iterate_end;
    }
    return;
  }

  SAVE_NORMAL_MAP_DATA(ptile, file, "map.res%03d",
		       resource_to_identifier(ptile->resource));

  special_halfbyte_iterate(j) {
    char buf[16]; /* enough for sprintf() below */
    enum tile_special_type mod[4];
    int l;

    for (l = 0; l < 4; l++) {
      mod[l] = MIN(4 * j + l, S_LAST);
    }
    sprintf (buf, "map.spe%02d_%%03d", j);
    SAVE_NORMAL_MAP_DATA(ptile, file, buf,
			 get_savegame_special(ptile->special, mod));
  } special_halfbyte_iterate_end;

  /* Store owner and ownership source as plain numbers */
  {
    int x, y;

    for (y = 0; y < map.ysize; y++) {
      char line[map.xsize * TOKEN_SIZE];

      line[0] = '\0';
      for (x = 0; x < map.xsize; x++) {
        char token[TOKEN_SIZE];
        struct tile *ptile = native_pos_to_tile(x, y);

        if (ptile->owner == NULL) {
          strcpy(token, "-");
        } else {
          my_snprintf(token, sizeof(token), "%d", ptile->owner->player_no);
        }
        strcat(line, token);
        if (x + 1 < map.xsize) {
          strcat(line, ",");
        }
      }
      secfile_insert_str(file, line, "map.owner%03d", y);
    }
    for (y = 0; y < map.ysize; y++) {
      char line[map.xsize * TOKEN_SIZE];

      line[0] = '\0';
      for (x = 0; x < map.xsize; x++) {
        char token[TOKEN_SIZE];
        struct tile *ptile = native_pos_to_tile(x, y);

        if (ptile->owner_source == NULL) {
          strcpy(token, "-");
        } else {
          my_snprintf(token, sizeof(token), "%d", ptile->owner_source->index);
        }
        strcat(line, token);
        if (x + 1 < map.xsize) {
          strcat(line, ",");
        }
      }
      secfile_insert_str(file, line, "map.source%03d", y);
    }
  }

  secfile_insert_bool(file, game.save_options.save_known, "game.save_known");
  if (game.save_options.save_known) {
    int known[MAP_INDEX_SIZE];

    /* HACK: we convert the data into a 32-bit integer, and then save it as
     * hex. */
    memset(known, 0, sizeof(known));
    whole_map_iterate(ptile) {
      players_iterate(pplayer) {
	if (map_is_known(ptile, pplayer)) {
	  known[ptile->index] |= (1u << pplayer->player_no);
	}
      } players_iterate_end;
    } whole_map_iterate_end;

    /* put 4-bit segments of the 32-bit "known" field */
    SAVE_NORMAL_MAP_DATA(ptile, file, "map.a%03d",
			 bin2ascii_hex(known[ptile->index], 0));
    SAVE_NORMAL_MAP_DATA(ptile, file, "map.b%03d",
			 bin2ascii_hex(known[ptile->index], 1));
    SAVE_NORMAL_MAP_DATA(ptile, file, "map.c%03d",
			 bin2ascii_hex(known[ptile->index], 2));
    SAVE_NORMAL_MAP_DATA(ptile, file, "map.d%03d",
			 bin2ascii_hex(known[ptile->index], 3));
    SAVE_NORMAL_MAP_DATA(ptile, file, "map.e%03d",
			 bin2ascii_hex(known[ptile->index], 4));
    SAVE_NORMAL_MAP_DATA(ptile, file, "map.g%03d",
			 bin2ascii_hex(known[ptile->index], 5));
    SAVE_NORMAL_MAP_DATA(ptile, file, "map.h%03d",
			 bin2ascii_hex(known[ptile->index], 6));
    SAVE_NORMAL_MAP_DATA(ptile, file, "map.i%03d",
			 bin2ascii_hex(known[ptile->index], 7));
  }
}

/*
 * Previously (with 1.14.1 and earlier) units had their type saved by ID.
 * This meant any time a unit was added (unless it was added at the end)
 * savegame compatibility would be broken.  Sometime after 1.14.1 this
 * method was changed so the type is saved by name.  However to preserve
 * backwards compatibility we have here a list of unit names from before
 * the change was made.  When loading an old savegame (one that doesn't
 * have the type string) we need to lookup the type into this array
 * to get the "proper" type string.  And when saving a new savegame we
 * insert the "old" type index into the array so that old servers can
 * load the savegame.
 *
 * Note that this list includes the AWACS, which was not in 1.14.1.
 */

/* old (~1.14.1) unit order in default/civ2/history ruleset */
static const char* old_default_unit_types[] = {
  "Settlers",	"Engineers",	"Warriors",	"Phalanx",
  "Archers",	"Legion",	"Pikemen",	"Musketeers",
  "Fanatics",	"Partisan",	"Alpine Troops","Riflemen",
  "Marines",	"Paratroopers",	"Mech. Inf.",	"Horsemen",
  "Chariot",	"Elephants",	"Crusaders",	"Knights",
  "Dragoons",	"Cavalry",	"Armor",	"Catapult",
  "Cannon",	"Artillery",	"Howitzer",	"Fighter",
  "Bomber",	"Helicopter",	"Stealth Fighter", "Stealth Bomber",
  "Trireme",	"Caravel",	"Galleon",	"Frigate",
  "Ironclad",	"Destroyer",	"Cruiser",	"AEGIS Cruiser",
  "Battleship",	"Submarine",	"Carrier",	"Transport",
  "Cruise Missile", "Nuclear",	"Diplomat",	"Spy",
  "Caravan",	"Freight",	"Explorer",	"Barbarian Leader",
  "AWACS"
};

/* old (~1.14.1) unit order in civ1 ruleset */
static const char* old_civ1_unit_types[] = {
  "Settlers",	"Engineers",	"Militia",	"Phalanx",
  "Archers",	"Legion",	"Pikemen",	"Musketeers",
  "Fanatics",	"Partisan",	"Alpine Troops","Riflemen",
  "Marines",	"Paratroopers",	"Mech. Inf.",	"Cavalry",
  "Chariot",	"Elephants",	"Crusaders",	"Knights",
  "Dragoons",	"Civ2-Cavalry",	"Armor",	"Catapult",
  "Cannon",	"Civ2-Artillery","Artillery",	"Fighter",
  "Bomber",	"Helicopter",	"Stealth Fighter", "Stealth Bomber",
  "Trireme",	"Sail",		"Galleon",	"Frigate",
  "Ironclad",	"Destroyer",	"Cruiser",	"AEGIS Cruiser",
  "Battleship",	"Submarine",	"Carrier",	"Transport",
  "Cruise Missile", "Nuclear",	"Diplomat",	"Spy",
  "Caravan",	"Freight",	"Explorer",	"Barbarian Leader"
};

/* old (1.14.1) improvement order in default ruleset */
const char* old_impr_types[] =
{
  "Airport",		"Aqueduct",		"Bank",
  "Barracks",		"Barracks II",		"Barracks III",
  "Cathedral",		"City Walls",		"Coastal Defense",
  "Colosseum",		"Courthouse",		"Factory",
  "Granary",		"Harbour",		"Hydro Plant",
  "Library",		"Marketplace",		"Mass Transit",
  "Mfg. Plant",		"Nuclear Plant",	"Offshore Platform",
  "Palace",		"Police Station",	"Port Facility",
  "Power Plant",	"Recycling Center",	"Research Lab",
  "SAM Battery",	"SDI Defense",		"Sewer System",
  "Solar Plant",	"Space Component",	"Space Module",
  "Space Structural",	"Stock Exchange",	"Super Highways",
  "Supermarket",	"Temple",		"University",
  "Apollo Program",	"A.Smith's Trading Co.","Colossus",
  "Copernicus' Observatory", "Cure For Cancer",	"Darwin's Voyage",
  "Eiffel Tower",	"Great Library",	"Great Wall",
  "Hanging Gardens",	"Hoover Dam",		"Isaac Newton's College",
  "J.S. Bach's Cathedral","King Richard's Crusade", "Leonardo's Workshop",
  "Lighthouse",		"Magellan's Expedition","Manhattan Project",
  "Marco Polo's Embassy","Michelangelo's Chapel","Oracle",
  "Pyramids",		"SETI Program",		"Shakespeare's Theatre",
  "Statue of Liberty",	"Sun Tzu's War Academy","United Nations",
  "Women's Suffrage",	"Coinage"
};

/* old (~1.14.1) techs order in civ2/default ruleset.
 *
 * Note that Theology is called Religion in civ1 ruleset; this is handled
 * as a special case in the code.
 *
 * Nowadays we save A_FUTURE as "A_FUTURE", A_NONE as "A_NONE".
 * A_UNSET as "A_UNSET" - they used to be saved as 198, 0 or -1, 0.
 */
const char* old_default_techs[] = 
{
  "A_NONE",
  "Advanced Flight",	"Alphabet",		"Amphibious Warfare",
  "Astronomy",		"Atomic Theory",	"Automobile",
  "Banking",		"Bridge Building",	"Bronze Working",
  "Ceremonial Burial",	"Chemistry",		"Chivalry",
  "Code of Laws",	"Combined Arms",	"Combustion",
  "Communism",		"Computers",		"Conscription",
  "Construction",	"Currency",		"Democracy",
  "Economics",		"Electricity",		"Electronics",
  "Engineering",	"Environmentalism",	"Espionage",
  "Explosives",		"Feudalism",		"Flight",
  "Fundamentalism",	"Fusion Power",		"Genetic Engineering",
  "Guerilla Warfare",	"Gunpowder",		"Horseback Riding",
  "Industrialization",	"Invention",		"Iron Working",
  "Labor Union",	"Laser",		"Leadership",
  "Literacy",		"Machine Tools",	"Magnetism",
  "Map Making",		"Masonry",		"Mass Production",
  "Mathematics",	"Medicine",		"Metallurgy",
  "Miniaturization",	"Mobile Warfare",	"Monarchy",
  "Monotheism",		"Mysticism",		"Navigation",
  "Nuclear Fission",	"Nuclear Power",	"Philosophy",
  "Physics",		"Plastics",		"Polytheism",
  "Pottery",		"Radio",		"Railroad",
  "Recycling",		"Refining",		"Refrigeration",
  "Robotics",		"Rocketry",		"Sanitation",
  "Seafaring",		"Space Flight",		"Stealth",
  "Steam Engine",	"Steel",		"Superconductors",
  "Tactics",		"The Corporation",	"The Republic",
  "The Wheel",		"Theology",		"Theory of Gravity",
  "Trade",		"University",		"Warrior Code",
  "Writing"
};

/* old (~1.14.1) government order in default, civ1, and history rulesets */
const char* old_default_governments[] = 
{
  "Anarchy", "Despotism", "Monarchy", "Communism", "Republic", "Democracy"
};

/* old (~1.14.1) government order in the civ2 ruleset */
const char* old_civ2_governments[] =
{
  "Anarchy", "Despotism", "Monarchy", "Communism", "Fundamentalism",
  "Republic", "Democracy"
};

/****************************************************************************
  Nowadays unit types are saved by name, but old servers need the
  unit_type_id.  This function tries to find the correct _old_ id for the
  unit's type.  It is used when the unit is saved.
****************************************************************************/
static Unit_type_id old_unit_type_id(const struct unit_type *type)
{
  const char** types;
  int num_types, i;

  if (strcmp(game.rulesetdir, "civ1") == 0) {
    types = old_civ1_unit_types;
    num_types = ARRAY_SIZE(old_civ1_unit_types);
  } else {
    types = old_default_unit_types;
    num_types = ARRAY_SIZE(old_default_unit_types);
  }

  for (i = 0; i < num_types; i++) {
    if (mystrcasecmp(unit_name_orig(type), types[i]) == 0) {
      return i;
    }
  }

  /* It's a new unit. Savegame cannot be forward compatible so we can
   * return anything */
  return type->index;
}

/****************************************************************************
  Convert an old-style unit type id into a unit type name.
****************************************************************************/
static const char* old_unit_type_name(int id)
{
  /* before 1.15.0 unit types used to be saved by id */
  if (id < 0) {
    freelog(LOG_ERROR, _("Wrong unit type id value (%d)"), id);
    exit(EXIT_FAILURE);
  }
  /* Different rulesets had different unit names. */
  if (strcmp(game.rulesetdir, "civ1") == 0) {
    if (id >= ARRAY_SIZE(old_civ1_unit_types)) {
      freelog(LOG_ERROR, _("Wrong unit type id value (%d)"), id);
      exit(EXIT_FAILURE);
    }
    return old_civ1_unit_types[id];
  } else {
    if (id >= ARRAY_SIZE(old_default_unit_types)) {
      freelog(LOG_ERROR, _("Wrong unit type id value (%d)"), id);
      exit(EXIT_FAILURE);
    }
    return old_default_unit_types[id];
  }
}

/****************************************************************************
  Nowadays improvement types are saved by name, but old servers need the
  Impr_type_id.  This function tries to find the correct _old_ id for the
  improvements's type.  It is used when the improvement is saved.
****************************************************************************/
static int old_impr_type_id(Impr_type_id type)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(old_impr_types); i++) {
    if (mystrcasecmp(get_improvement_name_orig(type),
                     old_impr_types[i]) == 0) {
      return i;
    }
  }

  /* It's a new improvement. Savegame cannot be forward compatible so we can
   * return anything */
  return type;
}

/***************************************************************
  Convert old-style improvement type id into improvement type name
***************************************************************/
static const char* old_impr_type_name(int id)
{
  /* before 1.15.0 improvement types used to be saved by id */
  if (id < 0 || id >= ARRAY_SIZE(old_impr_types)) {
    freelog(LOG_ERROR, _("Wrong improvement type id value (%d)"), id);
    exit(EXIT_FAILURE);
  }
  return old_impr_types[id];
}

/****************************************************************************
  Initialize the old-style improvement bitvector so that all improvements
  are marked as not present.
****************************************************************************/
static void init_old_improvement_bitvector(char* bitvector)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(old_impr_types); i++) {
    bitvector[i] = '0';
  }
  bitvector[ARRAY_SIZE(old_impr_types)] = '\0';
}

/****************************************************************************
  Insert improvement into old-style bitvector

  Improvement lists in cities and destroyed_wonders are saved as a
  bitvector in a string array.  New bitvectors do not depend on ruleset
  order. However, we want to create savegames which can be read by 
  1.14.x and earlier servers.  This function adds an improvement into the
  bitvector string according to the 1.14.1 improvement ordering.
****************************************************************************/
static void add_improvement_into_old_bitvector(char* bitvector,
                                               Impr_type_id id)
{
  int old_id;

  old_id = old_impr_type_id(id);
  if (old_id < 0 || old_id >= ARRAY_SIZE(old_impr_types)) {
    return;
  }
  bitvector[old_id] = '1';
}

/****************************************************************************
  Nowadays techs are saved by name, but old servers need numbers
  This function tries to find the correct _old_ id for the
  technology. It is used when the technology is saved.
****************************************************************************/
static int old_tech_id(Tech_type_id tech)
{
  const char* technology_name;
  int i;
  
  /* old (1.14.1) servers used to save it as 0 and interpret it from context */
  if (is_future_tech(tech)) {
    return 0;
  }

  /* old (1.14.1) servers used to save it as 0 and interpret it from context */  
  if (tech == A_UNSET) {
    return 0;
  }
  
  technology_name = advances[tech].name_orig;
  
  /* this is the only place where civ1 was different from 1.14.1 defaults */
  if (strcmp(game.rulesetdir, "civ1") == 0
      && mystrcasecmp(technology_name, "Religion") == 0) {
    return 83;
  }
  
  for (i = 0; i < ARRAY_SIZE(old_default_techs); i++) {
    if (mystrcasecmp(technology_name, old_default_techs[i]) == 0) {
      return i;
    }
  }
  
  /* It's a new technology. Savegame cannot be forward compatible so we can
   * return anything */
  return tech;
}

/****************************************************************************
  Convert an old-style technology id into a tech name.
****************************************************************************/
static const char* old_tech_name(int id)
{
  /* This was 1.14.1 value for A_FUTURE */
  if (id == 198) {
    return "A_FUTURE";
  }
  
  if (id == -1 || id == 0) {
    return "A_NONE";
  }
  
  if (id == A_UNSET) {
    return "A_UNSET";
  }

  if (id < 0 || id >= ARRAY_SIZE(old_default_techs)) {
    freelog(LOG_ERROR, _("Wrong tech type id value (%d)"), id);
    exit(EXIT_FAILURE);
  }

  if (strcmp(game.rulesetdir, "civ1") == 0 && id == 83) {
    return "Religion";
  }
  
  return old_default_techs[id];
}

/****************************************************************************
  Initialize the old-style technology bitvector so that all advances
  are marked as not present.
****************************************************************************/
static void init_old_technology_bitvector(char* bitvector)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(old_default_techs); i++) {
    bitvector[i] = '0';
  }
  bitvector[ARRAY_SIZE(old_default_techs)] = '\0';
}

/****************************************************************************
  Insert technology into old-style bitvector

  New bitvectors do not depend on ruleset order. However, we want to create
  savegames which can be read by 1.14.x and earlier servers. 
  This function adds a technology into the bitvector string according
  to the 1.14.1 technology ordering.
****************************************************************************/
static void add_technology_into_old_bitvector(char* bitvector,
                                              Tech_type_id id)
{
  int old_id;

  old_id = old_tech_id(id);
  if (old_id < 0 || old_id >= ARRAY_SIZE(old_default_techs)) {
    return;
  }
  bitvector[old_id] = '1';
}


/*****************************************************************************
  Load technology from path_name and if doesn't exist (because savegame
  is too old) load from path.
*****************************************************************************/
static Tech_type_id load_technology(struct section_file *file,
                                    const char* path, int plrno)
{
  char path_with_name[128];
  const char* name;
  int id;
  
  my_snprintf(path_with_name, sizeof(path_with_name), 
              "%s_name", path);
	      
  name = secfile_lookup_str_default(file, NULL, path_with_name, plrno);
  if (!name) {
    id = secfile_lookup_int_default(file, -1, path, plrno);
    name = old_tech_name(id);
  }
  
  if (mystrcasecmp(name, "A_FUTURE") == 0) {
    return A_FUTURE;
  }
  if (mystrcasecmp(name, "A_NONE") == 0) {
    return A_NONE;
  }
  if (mystrcasecmp(name, "A_UNSET") == 0) {
    return A_UNSET;
  }
  if (name[0] == '\0') {
    /* it is used by changed_from */
    return -1;
  }
  
  id = find_tech_by_name_orig(name);
  if (id == A_LAST) {
    freelog(LOG_ERROR, _("Unknown technology (%s)"), name);
    exit(EXIT_FAILURE);    
  }
  return id;
}

/*****************************************************************************
  Save technology in secfile entry called path_name and for forward
  compatibility in path(by number).
*****************************************************************************/
static void save_technology(struct section_file *file,
                            const char* path, int plrno, Tech_type_id tech)
{
  char path_with_name[128];
  const char* name;
 
  my_snprintf(path_with_name, sizeof(path_with_name), 
              "%s_name", path);
  
  switch (tech) {
    case -1: /* used in changed_from */
       name = "";
       break;
    case A_NONE:
      name = "A_NONE";
      break;
    case A_UNSET:
      name = "A_UNSET";
      break;
    case A_FUTURE:
      name = "A_FUTURE";
      break;
    default:
      name = advances[tech].name_orig;
      break;
  }
  secfile_insert_str(file, name, path_with_name, plrno);
  secfile_insert_int(file, old_tech_id(tech), path, plrno);
}

/****************************************************************************
  Nowadays governments are saved by name, but old servers need the
  index.  This function tries to find the correct _old_ id for the
  government. It is used when the government is saved.
****************************************************************************/
static int old_government_id(struct government *gov)
{
  const char** names;
  int num_names, i;

  if (strcmp(game.rulesetdir, "civ2") == 0) {
    names = old_civ2_governments;
    num_names = ARRAY_SIZE(old_civ2_governments);
  } else {
    names = old_default_governments;
    num_names = ARRAY_SIZE(old_default_governments);
  }

  for (i = 0; i < num_names; i++) {
    if (mystrcasecmp(gov->name_orig, names[i]) == 0) {
      return i;
    }
  }

  /* It's a new government. Savegame cannot be forward compatible so we can
   * return anything */
  return gov->index;
}

/****************************************************************************
  Convert an old-style government index into a government name.
****************************************************************************/
static const char* old_government_name(int id)
{
  /* before 1.15.0 governments used to be saved by index */
  if (id < 0) {
    freelog(LOG_ERROR, _("Wrong government type id value (%d)"), id);
    exit(EXIT_FAILURE);
  }
  /* Different rulesets had different governments. */
  if (strcmp(game.rulesetdir, "civ2") == 0) {
    if (id >= ARRAY_SIZE(old_civ2_governments)) {
      freelog(LOG_ERROR, _("Wrong government type id value (%d)"), id);
      exit(EXIT_FAILURE);
    }
    return old_civ2_governments[id];
  } else {
    if (id >= ARRAY_SIZE(old_default_governments)) {
      freelog(LOG_ERROR, _("Wrong government type id value (%d)"), id);
      exit(EXIT_FAILURE);
    }
    return old_default_governments[id];
  }
}

/****************************************************************************
  Loads the units for the given player.
****************************************************************************/
static void load_player_units(struct player *plr, int plrno,
			      struct section_file *file)
{
  int nunits, i, j;
  enum unit_activity activity;
  char *savefile_options = secfile_lookup_str(file, "savefile.options");

  plr->units = unit_list_new();
  nunits = secfile_lookup_int(file, "player%d.nunits", plrno);
  if (!plr->is_alive && nunits > 0) {
    nunits = 0; /* Some old savegames may be buggy. */
  }
  
  for (i = 0; i < nunits; i++) {
    struct unit *punit;
    struct city *pcity;
    int nat_x, nat_y;
    const char* type_name;
    struct unit_type *type;
    struct base_type *pbase = NULL;
    
    type_name = secfile_lookup_str_default(file, NULL, 
                                           "player%d.u%d.type_by_name",
					   plrno, i);
    if (!type_name) {
      /* before 1.15.0 unit types used to be saved by id. */
      int t = secfile_lookup_int(file, "player%d.u%d.type",
                             plrno, i);
      if (t < 0) {
        freelog(LOG_ERROR, _("Wrong player%d.u%d.type value (%d)"),
	        plrno, i, t);
	exit(EXIT_FAILURE);
      }
      type_name = old_unit_type_name(t);

    }
    
    type = find_unit_type_by_name_orig(type_name);
    if (!type) {
      freelog(LOG_ERROR, _("Unknown unit type '%s' in player%d section"),
              type_name, plrno);
      exit(EXIT_FAILURE);
    }
    
    punit = create_unit_virtual(plr, NULL, type,
	secfile_lookup_int(file, "player%d.u%d.veteran", plrno, i));
    punit->id = secfile_lookup_int(file, "player%d.u%d.id", plrno, i);
    alloc_id(punit->id);
    idex_register_unit(punit);

    nat_x = secfile_lookup_int(file, "player%d.u%d.x", plrno, i);
    nat_y = secfile_lookup_int(file, "player%d.u%d.y", plrno, i);
    punit->tile = native_pos_to_tile(nat_x, nat_y);

    punit->foul
      = secfile_lookup_bool_default(file, FALSE, "player%d.u%d.foul",
				    plrno, i);
    punit->homecity = secfile_lookup_int(file, "player%d.u%d.homecity",
					 plrno, i);

    if ((pcity = find_city_by_id(punit->homecity))) {
      unit_list_prepend(pcity->units_supported, punit);
    }
    
    punit->moves_left
      = secfile_lookup_int(file, "player%d.u%d.moves", plrno, i);
    punit->fuel = secfile_lookup_int(file, "player%d.u%d.fuel", plrno, i);
    punit->birth_turn = secfile_lookup_int_default(file, game.info.turn,
                                                   "player%d.u%d.born", plrno, i);
    activity = secfile_lookup_int(file, "player%d.u%d.activity", plrno, i);
    if (activity == ACTIVITY_PATROL_UNUSED) {
      /* Previously ACTIVITY_PATROL and ACTIVITY_GOTO were used for
       * client-side goto.  Now client-side goto is handled by setting
       * a special flag, and units with orders generally have ACTIVITY_IDLE.
       * Old orders are lost.  Old client-side goto units will still have
       * ACTIVITY_GOTO and will goto the correct position via server goto.
       * Old client-side patrol units lose their patrol routes and are put
       * into idle mode. */
      activity = ACTIVITY_IDLE;
    }

    if (activity == ACTIVITY_FORTRESS) {
      pbase = get_base_by_gui_type(BASE_GUI_FORTRESS, punit, punit->tile);
    } else if (activity == ACTIVITY_AIRBASE) {
      pbase = get_base_by_gui_type(BASE_GUI_AIRBASE, punit, punit->tile);
    } else if (activity == ACTIVITY_BASE) {
      /* This should currently not happen as ACTIVITY_BASE is saves as
       * ACTIVITY_FORTRESS or ACTIVITY_AIRBASE. We don't know base type,
       * let's use sensible fallback */
      set_unit_activity_base(punit, BASE_FORTRESS);
    } else {
      set_unit_activity(punit, activity);
    }

    if (pbase) {
      set_unit_activity_base(punit, pbase->id);
    }

    /* need to do this to assign/deassign settlers correctly -- Syela
     *
     * was punit->activity=secfile_lookup_int(file,
     *                             "player%d.u%d.activity",plrno, i); */
    punit->activity_count = secfile_lookup_int(file, 
					       "player%d.u%d.activity_count",
					       plrno, i);
    punit->activity_target
      = secfile_lookup_int_default(file, S_LAST,
				   "player%d.u%d.activity_target", plrno, i);

    if (activity == ACTIVITY_PILLAGE
        && (punit->activity_target == S_FORTRESS
            || punit->activity_target == S_AIRBASE)) {
      punit->activity_target = S_PILLAGE_BASE;
    }

    punit->done_moving = secfile_lookup_bool_default(file,
	(punit->moves_left == 0), "player%d.u%d.done_moving", plrno, i);

    punit->battlegroup
      = secfile_lookup_int_default(file, BATTLEGROUP_NONE,
				   "player%d.u%d.battlegroup", plrno, i);

    /* Load the goto information.  Older savegames will not have the
     * "go" field, so we just load the goto destination by default. */
    if (secfile_lookup_bool_default(file, TRUE,
				    "player%d.u%d.go", plrno, i)) {
      int nat_x = secfile_lookup_int(file, "player%d.u%d.goto_x", plrno, i);
      int nat_y = secfile_lookup_int(file, "player%d.u%d.goto_y", plrno, i);

      punit->goto_tile = native_pos_to_tile(nat_x, nat_y);
    } else {
      punit->goto_tile = NULL;
    }

    punit->ai.passenger
      = secfile_lookup_int_default(file, 0, "player%d.u%d.passenger", plrno, i);
    punit->ai.ferryboat
      = secfile_lookup_int_default(file, 0, "player%d.u%d.ferryboat", plrno, i);
    punit->ai.charge
      = secfile_lookup_int_default(file, 0, "player%d.u%d.charge", plrno, i);
    punit->ai.bodyguard
      = secfile_lookup_int_default(file, 0, "player%d.u%d.bodyguard", plrno, i);
    punit->ai.control
      = secfile_lookup_bool(file, "player%d.u%d.ai", plrno, i);
    punit->hp = secfile_lookup_int(file, "player%d.u%d.hp", plrno, i);
    
    punit->ord_map
      = secfile_lookup_int_default(file, 0,
				   "player%d.u%d.ord_map", plrno, i);
    punit->ord_city
      = secfile_lookup_int_default(file, 0,
				   "player%d.u%d.ord_city", plrno, i);
    punit->moved
      = secfile_lookup_bool_default(file, FALSE,
				    "player%d.u%d.moved", plrno, i);
    punit->paradropped
      = secfile_lookup_bool_default(file, FALSE,
				    "player%d.u%d.paradropped", plrno, i);
    punit->transported_by
      = secfile_lookup_int_default(file, -1, "player%d.u%d.transported_by",
				   plrno, i);
    /* Initialize upkeep values: these are hopefully initialized
       elsewhere before use (specifically, in city_support(); but
       fixme: check whether always correctly initialized?).
       Below is mainly for units which don't have homecity --
       otherwise these don't get initialized (and AI calculations
       etc may use junk values).
    */

    /* load the unit orders */
    if (has_capability("orders", savefile_options)) {
      int len = secfile_lookup_int_default(file, 0,
			"player%d.u%d.orders_length", plrno, i);
      if (len > 0) {
	char *orders_buf, *dir_buf, *act_buf;

	punit->orders.list = fc_malloc(len * sizeof(*(punit->orders.list)));
	punit->orders.length = len;
	punit->orders.index = secfile_lookup_int_default(file, 0,
			"player%d.u%d.orders_index", plrno, i);
	punit->orders.repeat = secfile_lookup_bool_default(file, FALSE,
			"player%d.u%d.orders_repeat", plrno, i);
	punit->orders.vigilant = secfile_lookup_bool_default(file, FALSE,
			"player%d.u%d.orders_vigilant", plrno, i);

	orders_buf = secfile_lookup_str_default(file, "",
			"player%d.u%d.orders_list", plrno, i);
	dir_buf = secfile_lookup_str_default(file, "",
			"player%d.u%d.dir_list", plrno, i);
	act_buf = secfile_lookup_str_default(file, "",
			"player%d.u%d.activity_list", plrno, i);
	punit->has_orders = TRUE;
	for (j = 0; j < len; j++) {
	  struct unit_order *order = &punit->orders.list[j];
          struct base_type *pbase = NULL;

	  if (orders_buf[j] == '\0' || dir_buf[j] == '\0'
	      || act_buf[j] == '\0') {
	    freelog(LOG_ERROR, _("Savegame error: invalid unit orders."));
	    free_unit_orders(punit);
	    break;
	  }
	  order->order = char2order(orders_buf[j]);
	  order->dir = char2dir(dir_buf[j]);
	  order->activity = char2activity(act_buf[j]);
	  if (order->order == ORDER_LAST
	      || (order->order == ORDER_MOVE && order->dir == DIR8_LAST)
	      || (order->order == ORDER_ACTIVITY
		  && order->activity == ACTIVITY_LAST)) {
	    /* An invalid order.  Just drop the orders for this unit. */
	    free(punit->orders.list);
	    punit->orders.list = NULL;
	    punit->has_orders = FALSE;
	    break;
	  }

          if (order->activity == ACTIVITY_FORTRESS) {
            pbase = get_base_by_gui_type(BASE_GUI_FORTRESS, NULL, NULL);
            order->activity = ACTIVITY_IDLE; /* In case no matching gui_type found */
          } else if (order->activity == ACTIVITY_AIRBASE) {
            pbase = get_base_by_gui_type(BASE_GUI_AIRBASE, NULL, NULL);
            order->activity = ACTIVITY_IDLE; /* In case no matching gui_type found */
          }

          if (pbase) {
            /* Either ACTIVITY_FORTRESS or ACTIVITY_AIRBASE */
            order->activity = ACTIVITY_BASE;
            order->base = pbase->id;
          }
	}
      } else {
	punit->has_orders = FALSE;
	punit->orders.list = NULL;
      }
    } else {
      /* Old-style goto routes get discarded. */
      punit->has_orders = FALSE;
      punit->orders.list = NULL;
    }

    /* allocate the unit's contribution to fog of war */
    punit->server.vision = vision_new(punit->owner, punit->tile, TRUE);
    unit_refresh_vision(punit);
    /* NOTE: There used to be some map_set_known calls here.  These were
     * unneeded since unfogging the tile when the unit sees it will
     * automatically reveal that tile. */

    unit_list_append(plr->units, punit);

    unit_list_prepend(punit->tile->units, punit);
  }
}

/****************************************************************************
  Load all information about player "plrno" into the structure pointed to
  by "plr".
****************************************************************************/
static void player_load(struct player *plr, int plrno,
			struct section_file *file,
			char** improvement_order,
			int improvement_order_size,
			char** technology_order,
			int technology_order_size)
{
  int i, j, k, x, y, ncities, c_s;
  const char *p;
  const char *name;
  char *savefile_options = secfile_lookup_str(file, "savefile.options");
  struct ai_data *ai;
  struct government *gov;
  int id;
  struct team *pteam;
  struct player_research *research;
  struct nation_type *pnation;

  /* not all players have teams */
  id = secfile_lookup_int_default(file, -1, "player%d.team_no", plrno);
  pteam = team_get_by_id(id);
  if (pteam == NULL) {
    pteam = find_empty_team();
  }
  
  team_add_player(plr, pteam);
  
  research = get_player_research(plr);

  server_player_init(plr, TRUE, FALSE);
  ai = ai_data_get(plr);

  plr->ai.barbarian_type = secfile_lookup_int_default(file, 0, "player%d.ai.is_barbarian",
                                                    plrno);
  if (is_barbarian(plr)) game.info.nbarbarians++;

  sz_strlcpy(plr->name, secfile_lookup_str(file, "player%d.name", plrno));
  sz_strlcpy(plr->username,
	     secfile_lookup_str_default(file, "", "player%d.username", plrno));
  sz_strlcpy(plr->ranked_username,
	     secfile_lookup_str_default(file, "", "player%d.ranked_username", 
             plrno));

  /* 1.15 and later versions store nations by name.  Try that first. */
  p = secfile_lookup_str_default(file, NULL, "player%d.nation", plrno);
  if (!p) {
    /*
     * Otherwise read as a pre-1.15 savefile with numeric nation indexes.
     * This random-looking order is from the old nations/ruleset file.
     * Use it to convert old-style nation indices to name strings.
     * The idea is not to be dependent on the order in which nations 
     * get read into the registry.
     */
    const char *name_order[] = {
      "roman", "babylonian", "german", "egyptian", "american", "greek",
      "indian", "russian", "zulu", "french", "aztec", "chinese", "english",
      "mongol", "turk", "spanish", "persian", "arab", "carthaginian", "inca",
      "viking", "polish", "hungarian", "danish", "dutch", "swedish",
      "japanese", "portuguese", "finnish", "sioux", "czech", "australian",
      "welsh", "korean", "scottish", "israeli", "argentine", "canadian",
      "ukrainian", "lithuanian", "kenyan", "dunedain", "vietnamese", "thai",
      "mordor", "bavarian", "brazilian", "irish", "cornish", "italian",
      "filipino", "estonian", "latvian", "boer", "silesian", "singaporean",
      "chilean", "catalan", "croatian", "slovenian", "serbian", "barbarian",
    };
    int index = secfile_lookup_int(file, "player%d.race", plrno);

    if (index >= 0 && index < ARRAY_SIZE(name_order)) {
      p = name_order[index];
    } else {
      p = "";
    }
  }
  pnation = find_nation_by_name_orig(p);

  if (pnation != NO_NATION_SELECTED) {
    player_set_nation(plr, pnation);
  } else {
    plr->nation = NO_NATION_SELECTED;
  }
  /* Nation may be unselected at this point; we check for this later and
   * reassign nations to players who don't have them. */

  /* Add techs from game and nation, but ignore game.info.tech. */
  init_tech(plr);
  /* We used to call give_initial_techs here, but that shouldn't be
   * necessary.  The savegame should already mark those techs as known.
   * give_initial_techs will crash if the nation is unset. */

  /* It is important that barbarian_type is loaded before
   * calling is_barbarian() and is_land_barbarian() */
  if (is_barbarian(plr) && plr->nation == NO_NATION_SELECTED) {
    if (is_land_barbarian(plr)) {
      pnation = pick_a_nation(NULL, FALSE, TRUE, LAND_BARBARIAN);
    } else {
      pnation = pick_a_nation(NULL, FALSE, TRUE, SEA_BARBARIAN);
    }
    player_set_nation(plr, pnation);
  }

  /* government */
  name = secfile_lookup_str_default(file, NULL, "player%d.government_name",
                                    plrno);
  if (!name) {
    /* old servers used to save government by id */
    id = secfile_lookup_int(file, "player%d.government", plrno);
    name = old_government_name(id);
  }
  gov = find_government_by_name_orig(name);
  if (gov == NULL) {
    freelog(LOG_ERROR, _("Unsupported government found (%s)"), name);
    exit(EXIT_FAILURE);
  }
  plr->government = gov;

  /* Target government */
  name = secfile_lookup_str_default(file, NULL,
				    "player%d.target_government_name",
				    plrno);
  if (name) {
    gov = find_government_by_name_orig(name);
  } else {
    gov = NULL;
  }
  if (gov) {
    plr->target_government = gov;
  } else {
    /* Old servers didn't have this value. */
    plr->target_government = plr->government;
  }

  BV_CLR_ALL(plr->embassy);
  if (has_capability("embassies", savefile_options)) {
    players_iterate(pother) {
      if (secfile_lookup_bool(file, "player%d.embassy%d",
			      plrno, pother->player_no)) {
	BV_SET(plr->embassy, pother->player_no);
      }
    } players_iterate_end;
  } else {
    /* Required for 2.0 and earlier savegames.  Remove eventually and make
     * the cap check manditory. */
    int embassy = secfile_lookup_int(file, "player%d.embassy", plrno);

    players_iterate(pother) {
      if (embassy & (1 << pother->player_no)) {
	BV_SET(plr->embassy, pother->player_no);
      }
    } players_iterate_end;

  }

  p = secfile_lookup_str_default(file, NULL, "player%d.city_style_by_name",
                                 plrno);
  if (!p) {
    char* old_order[4] = {"European", "Classical", "Tropical", "Asian"};
    c_s = secfile_lookup_int_default(file, 0, "player%d.city_style", plrno);
    if (c_s < 0 || c_s > 3) {
      c_s = 0;
    }
    p = old_order[c_s];
  }
  c_s = get_style_by_name_orig(p);
  if (c_s == -1) {
    freelog(LOG_ERROR, _("Unsupported city style found in player%d section. "
                         "Changed to %s"), plrno, get_city_style_name(0));
    c_s = 0;
  }	
  plr->city_style = c_s;

  plr->nturns_idle=0;
  plr->is_male=secfile_lookup_bool_default(file, TRUE, "player%d.is_male", plrno);
  plr->is_alive=secfile_lookup_bool(file, "player%d.is_alive", plrno);
  /* "Old" observer players will still be loaded but are considered dead. */
  plr->ai.control = secfile_lookup_bool(file, "player%d.ai.control", plrno);
  for (i = 0; i < MAX_NUM_PLAYERS; i++) {
    plr->ai.love[i]
         = secfile_lookup_int_default(file, 1, "player%d.ai%d.love", plrno, i);
    ai->diplomacy.player_intel[i].spam 
         = secfile_lookup_int_default(file, 0, "player%d.ai%d.spam", plrno, i);
    ai->diplomacy.player_intel[i].countdown
         = secfile_lookup_int_default(file, -1, "player%d.ai%d.countdown", plrno, i);
    ai->diplomacy.player_intel[i].war_reason
         = secfile_lookup_int_default(file, 0, "player%d.ai%d.war_reason", plrno, i);
    ai->diplomacy.player_intel[i].ally_patience
         = secfile_lookup_int_default(file, 0, "player%d.ai%d.patience", plrno, i);
    ai->diplomacy.player_intel[i].warned_about_space
         = secfile_lookup_int_default(file, 0, "player%d.ai%d.warn_space", plrno, i);
    ai->diplomacy.player_intel[i].asked_about_peace
         = secfile_lookup_int_default(file, 0, "player%d.ai%d.ask_peace", plrno, i);
    ai->diplomacy.player_intel[i].asked_about_alliance
         = secfile_lookup_int_default(file, 0, "player%d.ai%d.ask_alliance", plrno, i);
    ai->diplomacy.player_intel[i].asked_about_ceasefire
         = secfile_lookup_int_default(file, 0, "player%d.ai%d.ask_ceasefire", plrno, i);
  }

  /* Backwards-compatibility: the tech goal value is still stored in the
   * "ai" section even though it was moved into the research struct. */
  research->tech_goal
    = load_technology(file, "player%d.ai.tech_goal", plrno);

  if (research->tech_goal == A_NONE) {
    /* Old servers (1.14.1) saved both A_UNSET and A_NONE by 0
     * Here 0 means A_UNSET
     */
    research->tech_goal = A_UNSET;
  }
  /* Some sane defaults */
  plr->ai.handicap = 0;		/* set later */
  plr->ai.fuzzy = 0;		/* set later */
  plr->ai.expand = 100;		/* set later */
  plr->ai.science_cost = 100;	/* set later */
  plr->ai.skill_level =
    secfile_lookup_int_default(file, game.info.skill_level,
			       "player%d.ai.skill_level", plrno);
  if (plr->ai.control && plr->ai.skill_level==0) {
    plr->ai.skill_level = GAME_OLD_DEFAULT_SKILL_LEVEL;
  }
  if (plr->ai.control) {
    /* Set AI parameters */
    set_ai_level_directer(plr, plr->ai.skill_level);
  }

  plr->economic.gold=secfile_lookup_int(file, "player%d.gold", plrno);
  plr->economic.tax=secfile_lookup_int(file, "player%d.tax", plrno);
  plr->economic.science=secfile_lookup_int(file, "player%d.science", plrno);
  plr->economic.luxury=secfile_lookup_int(file, "player%d.luxury", plrno);
  
  plr->bulbs_last_turn =
    secfile_lookup_int_default(file, 0,
                               "player%d.bulbs_last_turn",
			       plrno);

  /* how many future techs were researched already by player */
  research->future_tech
    = secfile_lookup_int(file, "player%d.futuretech", plrno);

  /* We use default values for bulbs_researched_before, changed_from
   * and got_tech to preserve backwards-compatibility with save files
   * that didn't store this information. */
  research->bulbs_researched
    = secfile_lookup_int(file, "player%d.researched", plrno);
  research->bulbs_researched_before =
	  secfile_lookup_int_default(file, 0,
				     "player%d.researched_before", plrno);
  research->changed_from = 
          load_technology(file, "player%d.research_changed_from", plrno);
  research->got_tech = secfile_lookup_bool_default(file, FALSE,
					      "player%d.research_got_tech",
					      plrno);
  research->techs_researched
    = secfile_lookup_int(file, "player%d.researchpoints", plrno);
  research->researching
    = load_technology(file, "player%d.researching", plrno);
  if (research->researching == A_NONE) {
    /* Old servers (1.14.1) used to save A_FUTURE by 0 
     * This has to be interpreted from context because A_NONE was also
     * saved by 0
     */
    research->researching = A_FUTURE;
  }
  
  p = secfile_lookup_str_default(file, NULL, "player%d.invs_new", plrno);
  if (!p) {
    /* old savegames */
    p = secfile_lookup_str(file, "player%d.invs", plrno);
    for (k = 0; p[k];  k++) {
      if (p[k] == '1') {
	name = old_tech_name(k);
	id = find_tech_by_name_orig(name);
	if (id != A_LAST) {
	  set_invention(plr, id, TECH_KNOWN);
	}
      }
    }
  } else {
    for (k = 0; k < technology_order_size && p[k]; k++) {
      if (p[k] == '1') {
	id = find_tech_by_name_orig(technology_order[k]);
	if (id != A_LAST) {
	  set_invention(plr, id, TECH_KNOWN);
	}
      }
    }
  }
    
  plr->capital = secfile_lookup_bool(file, "player%d.capital", plrno);

  {
    /* The old-style "revolution" value indicates the number of turns until
     * the revolution is complete, or 0 if there is no revolution.  The
     * new-style "revolution_finishes" value indicates the turn in which
     * the revolution will complete (which may be less than the current
     * turn) or -1 if there is no revolution. */
    int revolution = secfile_lookup_int_default(file, 0, "player%d.revolution",
						plrno);

    if (revolution == 0) {
      if (plr->government != game.government_when_anarchy) {
        revolution = -1;
      } else {
        /* some old savegames may be buggy */
        revolution = game.info.turn + 1;
      }
    } else {
      revolution = game.info.turn + revolution;
    }
    plr->revolution_finishes
      = secfile_lookup_int_default(file, revolution,
				   "player%d.revolution_finishes", plrno);
  }

  update_research(plr);

  for (i = 0; i < game.info.nplayers; i++) {
    plr->diplstates[i].type = 
      secfile_lookup_int_default(file, DS_WAR,
				 "player%d.diplstate%d.type", plrno, i);
    plr->diplstates[i].max_state = 
      secfile_lookup_int_default(file, DS_WAR,
				 "player%d.diplstate%d.max_state", plrno, i);
    plr->diplstates[i].first_contact_turn = 
      secfile_lookup_int_default(file, 0,
				 "player%d.diplstate%d.first_contact_turn", plrno, i);
    plr->diplstates[i].turns_left = 
      secfile_lookup_int_default(file, -2,
				 "player%d.diplstate%d.turns_left", plrno, i);
    plr->diplstates[i].has_reason_to_cancel = 
      secfile_lookup_int_default(file, 0,
				 "player%d.diplstate%d.has_reason_to_cancel",
				 plrno, i);
    plr->diplstates[i].contact_turns_left = 
      secfile_lookup_int_default(file, 0,
			   "player%d.diplstate%d.contact_turns_left", plrno, i);
  }
  /* We don't need this info, but savegames carry it anyway.
     To avoid getting "unused" warnings we touch the values like this. */
  for (i = game.info.nplayers; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
    secfile_lookup_int_default(file, DS_WAR,
			       "player%d.diplstate%d.type", plrno, i);
    secfile_lookup_int_default(file, 0,
			       "player%d.diplstate%d.turns_left", plrno, i);
    secfile_lookup_int_default(file, 0,
			       "player%d.diplstate%d.has_reason_to_cancel",
			       plrno, i);
    secfile_lookup_int_default(file, 0,
			   "player%d.diplstate%d.contact_turns_left", plrno, i);
  }

  { /* spacerace */
    struct player_spaceship *ship = &plr->spaceship;
    char prefix[32];
    char *st;
    
    my_snprintf(prefix, sizeof(prefix), "player%d.spaceship", plrno);
    spaceship_init(ship);
    ship->state = secfile_lookup_int(file, "%s.state", prefix);

    if (ship->state != SSHIP_NONE) {
      ship->structurals = secfile_lookup_int(file, "%s.structurals", prefix);
      ship->components = secfile_lookup_int(file, "%s.components", prefix);
      ship->modules = secfile_lookup_int(file, "%s.modules", prefix);
      ship->fuel = secfile_lookup_int(file, "%s.fuel", prefix);
      ship->propulsion = secfile_lookup_int(file, "%s.propulsion", prefix);
      ship->habitation = secfile_lookup_int(file, "%s.habitation", prefix);
      ship->life_support = secfile_lookup_int(file, "%s.life_support", prefix);
      ship->solar_panels = secfile_lookup_int(file, "%s.solar_panels", prefix);

      st = secfile_lookup_str(file, "%s.structure", prefix);
      for (i = 0; i < NUM_SS_STRUCTURALS; i++) {
	if (st[i] == '0') {
	  ship->structure[i] = FALSE;
	} else if (st[i] == '1') {
	  ship->structure[i] = TRUE;
	} else {
	  freelog(LOG_ERROR, "invalid spaceship structure '%c' %d", st[i],
		  st[i]);
	  ship->structure[i] = FALSE;
	}
      }
      if (ship->state >= SSHIP_LAUNCHED) {
	ship->launch_year = secfile_lookup_int(file, "%s.launch_year", prefix);
      }
      spaceship_calc_derived(ship);
    }
  }

  plr->cities = city_list_new();
  ncities=secfile_lookup_int(file, "player%d.ncities", plrno);
  if (!plr->is_alive && ncities > 0) {
    ncities = 0; /* Some old savegames may be buggy. */
  }

  for (i = 0; i < ncities; i++) { /* read the cities */
    struct city *pcity;
    int nat_x = secfile_lookup_int(file, "player%d.c%d.x", plrno, i);
    int nat_y = secfile_lookup_int(file, "player%d.c%d.y", plrno, i);
    struct tile *ptile = native_pos_to_tile(nat_x, nat_y);
    const char* name;
    int id, k;

    pcity = create_city_virtual(plr, ptile,
                      secfile_lookup_str(file, "player%d.c%d.name", plrno, i));
    ptile->owner_source = pcity->tile;
    tile_set_owner(ptile, pcity->owner);

    pcity->id=secfile_lookup_int(file, "player%d.c%d.id", plrno, i);
    alloc_id(pcity->id);
    idex_register_city(pcity);

    id = secfile_lookup_int_default(file, -1,
				    "player%d.c%d.original", plrno, i);
    if (id >= 0 && id < game.info.nplayers) {
      pcity->original = get_player(id);
    } else {
      pcity->original = get_player(plrno);
    }

    pcity->size=secfile_lookup_int(file, "player%d.c%d.size", plrno, i);

    pcity->steal=secfile_lookup_int(file, "player%d.c%d.steal", plrno, i);

    specialist_type_iterate(sp) {
      pcity->specialists[sp]
	= secfile_lookup_int(file, "player%d.c%d.n%s", plrno, i,
			     get_specialist(sp)->name);
    } specialist_type_iterate_end;

    for (j = 0; j < NUM_TRADEROUTES; j++)
      pcity->trade[j]=secfile_lookup_int(file, "player%d.c%d.traderoute%d",
					 plrno, i, j);
    
    pcity->food_stock = secfile_lookup_int(file, "player%d.c%d.food_stock", 
					   plrno, i);
    pcity->shield_stock = secfile_lookup_int(file,
					     "player%d.c%d.shield_stock", 
					     plrno, i);
    pcity->anarchy = secfile_lookup_int(file, "player%d.c%d.anarchy",
					plrno, i);
    pcity->rapture = secfile_lookup_int_default(file, 0,
						"player%d.c%d.rapture",
						plrno, i);
    pcity->was_happy = secfile_lookup_bool(file,
					   "player%d.c%d.was_happy",
					   plrno, i);
    pcity->production.is_unit =
      secfile_lookup_bool(file, 
			 "player%d.c%d.is_building_unit", plrno, i);
    name = secfile_lookup_str_default(file, NULL,
				      "player%d.c%d.currently_building_name",
				      plrno, i);
    if (pcity->production.is_unit) {
      if (!name) {
	id = secfile_lookup_int(file, "player%d.c%d.currently_building", 
				plrno, i);
	name = old_unit_type_name(id);
      }
      pcity->production.value = find_unit_type_by_name_orig(name)->index;
    } else {
      if (!name) {
	id = secfile_lookup_int(file, "player%d.c%d.currently_building",
				plrno, i);
	name = old_impr_type_name(id);
      }
      pcity->production.value = find_improvement_by_name_orig(name);
    }

    if (has_capability("turn_last_built", savefile_options)) {
      pcity->turn_last_built = secfile_lookup_int(file,
				"player%d.c%d.turn_last_built", plrno, i);
    } else {
      /* Before, turn_last_built was stored as a year.  There is no easy
       * way to convert this into a turn value. */
      pcity->turn_last_built = 0;
    }
    pcity->changed_from.is_unit =
      secfile_lookup_bool_default(file, pcity->production.is_unit,
				 "player%d.c%d.changed_from_is_unit",
				  plrno, i);
    name = secfile_lookup_str_default(file, NULL,
				      "player%d.c%d.changed_from_name",
				      plrno, i);
    if (pcity->changed_from.is_unit) {
      if (!name) {
	id = secfile_lookup_int(file, "player%d.c%d.changed_from_id", 
				plrno, i);
	name = old_unit_type_name(id);
      }
      pcity->changed_from.value = find_unit_type_by_name_orig(name)->index;
    } else {
      if (!name) {
	id = secfile_lookup_int(file, "player%d.c%d.changed_from_id",
				plrno, i);
	name = old_impr_type_name(id);
      }
      pcity->changed_from.value = find_improvement_by_name_orig(name);
    }
			 
    pcity->before_change_shields=
      secfile_lookup_int_default(file, pcity->shield_stock,
				 "player%d.c%d.before_change_shields", plrno, i);
    pcity->disbanded_shields=
      secfile_lookup_int_default(file, 0,
				 "player%d.c%d.disbanded_shields", plrno, i);
    pcity->caravan_shields=
      secfile_lookup_int_default(file, 0,
				 "player%d.c%d.caravan_shields", plrno, i);
    pcity->last_turns_shield_surplus =
      secfile_lookup_int_default(file, 0,
				 "player%d.c%d.last_turns_shield_surplus",
				 plrno, i);

    pcity->synced = FALSE; /* must re-sync with clients */

    pcity->turn_founded =
	secfile_lookup_int_default(file, -2, "player%d.c%d.turn_founded",
				   plrno, i);

    j = secfile_lookup_int(file, "player%d.c%d.did_buy", plrno, i);
    pcity->did_buy = (j != 0);
    if (j == -1 && pcity->turn_founded == -2) {
      pcity->turn_founded = game.info.turn;
    }

    pcity->did_sell =
      secfile_lookup_bool_default(file, FALSE, "player%d.c%d.did_sell", plrno,i);
    
    pcity->airlift = secfile_lookup_bool_default(file, FALSE,
					"player%d.c%d.airlift", plrno,i);

    /* New city-options form as of freeciv 2.1.  Old (<= 2.0) city options
     * are lost on upgrade. */
    BV_CLR_ALL(pcity->city_options);
    for (j = 0; j < CITYO_LAST; j++) {
      if (secfile_lookup_bool_default(file, FALSE,
				      "player%d.c%d.option%d",
				      plrno, i, j)) {
	BV_SET(pcity->city_options, j);
      }
    }

    /* Fix for old buggy savegames. */
    if (!has_capability("known32fix", savefile_options)
	&& plrno >= 16) {
      map_city_radius_iterate(pcity->tile, tile1) {
	map_set_known(tile1, plr);
      } map_city_radius_iterate_end;
    }
    
    /* adding the cities contribution to fog-of-war */
    pcity->server.vision = vision_new(pcity->owner, pcity->tile, FALSE);
    city_refresh_vision(pcity);

    pcity->units_supported = unit_list_new();

    /* Initialize pcity->city_map[][], using set_worker_city() so that
       ptile->worked gets initialized correctly.  The pre-initialisation
       to C_TILE_EMPTY is necessary because set_worker_city() accesses
       the existing value to possibly adjust ptile->worked, so need to
       initialize a non-worked value so ptile->worked (possibly already
       set from neighbouring city) does not get unset for C_TILE_EMPTY
       or C_TILE_UNAVAILABLE here.  -- dwp
    */
    p=secfile_lookup_str(file, "player%d.c%d.workers", plrno, i);
    for(y=0; y<CITY_MAP_SIZE; y++) {
      for(x=0; x<CITY_MAP_SIZE; x++) {
	pcity->city_map[x][y] =
	    is_valid_city_coords(x, y) ? C_TILE_EMPTY : C_TILE_UNAVAILABLE;
	if (*p == '0') {
	  set_worker_city(pcity, x, y,
			  city_map_to_map(pcity, x, y) ?
			  C_TILE_EMPTY : C_TILE_UNAVAILABLE);
	} else if (*p=='1') {
	  struct tile *ptile;

	  ptile = city_map_to_map(pcity, x, y);

	  if (ptile->worked) {
	    /* oops, inconsistent savegame; minimal fix: */
	    freelog(LOG_VERBOSE, "Inconsistent worked for %s (%d,%d), "
		    "converting to specialist", pcity->name, x, y);
	    pcity->specialists[DEFAULT_SPECIALIST]++;
	    set_worker_city(pcity, x, y, C_TILE_UNAVAILABLE);
	  } else {
	    set_worker_city(pcity, x, y, C_TILE_WORKER);
	  }
	} else {
	  assert(*p == '2');
	  if (is_valid_city_coords(x, y)) {
	    set_worker_city(pcity, x, y, C_TILE_UNAVAILABLE);
	  }
	  assert(pcity->city_map[x][y] == C_TILE_UNAVAILABLE);
	}
        p++;
      }
    }

    /* Initialise list of improvements with City- and Building-wide
       equiv_ranges */
    for (k = 0; k < ARRAY_SIZE(pcity->improvements); k++) {
      pcity->improvements[k] = I_NONE;
    }

    p = secfile_lookup_str_default(file, NULL,
				   "player%d.c%d.improvements_new",
                                   plrno, i);
    if (!p) {
      /* old savegames */
      p = secfile_lookup_str(file, "player%d.c%d.improvements", plrno, i);
      for (k = 0; p[k]; k++) {
        if (p[k] == '1') {
	  name = old_impr_type_name(k);
	  id = find_improvement_by_name_orig(name);
	  if (id != -1) {
	    city_add_improvement(pcity, id);
	  }
	}
      }
    } else {
      for (k = 0; k < improvement_order_size && p[k]; k++) {
        if (p[k] == '1') {
	  id = find_improvement_by_name_orig(improvement_order[k]);
	  if (id != -1) {
	    city_add_improvement(pcity, id);
	  }
	}
      }
    }

    init_worklist(&pcity->worklist);
    worklist_load(file, &pcity->worklist, "player%d.c%d", plrno, i);

    /* FIXME: remove this when the urgency is properly recalculated. */
    pcity->ai.urgency = secfile_lookup_int_default(file, 0, 
				"player%d.c%d.ai.urgency", plrno, i);

    tile_set_city(pcity->tile, pcity);

    city_list_append(plr->cities, pcity);
  }

  load_player_units(plr, plrno, file);

  if (section_file_lookup(file, "player%d.attribute_v2_block_length", plrno)) {
    int raw_length1, raw_length2, part_nr, parts;
    size_t quoted_length;
    char *quoted;

    raw_length1 =
	secfile_lookup_int(file, "player%d.attribute_v2_block_length", plrno);
    if (plr->attribute_block.data) {
      free(plr->attribute_block.data);
      plr->attribute_block.data = NULL;
    }
    plr->attribute_block.data = fc_malloc(raw_length1);
    plr->attribute_block.length = raw_length1;

    quoted_length = secfile_lookup_int
	(file, "player%d.attribute_v2_block_length_quoted", plrno);
    quoted = fc_malloc(quoted_length + 1);
    quoted[0] = 0;

    parts =
	secfile_lookup_int(file, "player%d.attribute_v2_block_parts", plrno);

    for (part_nr = 0; part_nr < parts; part_nr++) {
      char *current = secfile_lookup_str(file,
					 "player%d.attribute_v2_block_data.part%d",
					 plrno, part_nr);
      if (!current)
	break;
      freelog(LOG_DEBUG, "quoted_length=%lu quoted=%lu current=%lu",
	      (unsigned long) quoted_length,
	      (unsigned long) strlen(quoted),
	      (unsigned long) strlen(current));
      assert(strlen(quoted) + strlen(current) <= quoted_length);
      strcat(quoted, current);
    }
    if (quoted_length != strlen(quoted)) {
      freelog(LOG_NORMAL, "quoted_length=%lu quoted=%lu",
	      (unsigned long) quoted_length,
	      (unsigned long) strlen(quoted));
      assert(0);
    }

    raw_length2 =
	unquote_block(quoted,
		      plr->attribute_block.data,
		      plr->attribute_block.length);
    assert(raw_length1 == raw_length2);
    free(quoted);
  }
}

/********************************************************************** 
The private map for fog of war
***********************************************************************/
static void player_map_load(struct player *plr, int plrno,
		      struct section_file *file,
		      char** improvement_order, int improvement_order_size,
		      const enum tile_special_type *special_order)
{
  int i;

  if (!plr->is_alive)
    map_know_and_see_all(plr);

  /* load map if:
     1) it from a fog of war build
     2) fog of war was on (otherwise the private map wasn't saved)
     3) is not from a "unit only" fog of war save file
  */
  if (secfile_lookup_int_default(file, -1, "game.fogofwar") != -1
      && game.info.fogofwar == TRUE
      && secfile_lookup_int_default(file, -1,"player%d.total_ncities", plrno) != -1
      && secfile_lookup_bool_default(file, TRUE, "game.save_private_map")) {
    LOAD_MAP_DATA(ch, nat_y, ptile,
		  secfile_lookup_str(file, "player%d.map_t%03d",
				     plrno, nat_y),
		  map_get_player_tile(ptile, plr)->terrain =
		  char2terrain(ch));

    if (special_order) {
      LOAD_MAP_DATA(ch, nat_y, ptile,
	secfile_lookup_str(file, "player%d.map_res%03d", plrno, nat_y),
	map_get_player_tile(ptile, plr)->resource
		    = identifier_to_resource(ch));

      special_halfbyte_iterate(j) {
	char buf[32]; /* enough for sprintf() below */
	sprintf (buf, "player%d.map_spe%02d_%%03d", plrno, j);
	LOAD_MAP_DATA(ch, nat_y, ptile,
	    secfile_lookup_str(file, buf, nat_y),
	    set_savegame_special(&map_get_player_tile(ptile, plr)->special,
				 ch, special_order + 4 * j));
      } special_halfbyte_iterate_end;
    } else {
      /* get 4-bit segments of 12-bit "special" field. */
      LOAD_MAP_DATA(ch, nat_y, ptile,
	  secfile_lookup_str(file, "player%d.map_l%03d", plrno, nat_y),
	  set_savegame_special(&map_get_player_tile(ptile, plr)->special,
			       ch, default_specials + 0));
      LOAD_MAP_DATA(ch, nat_y, ptile,
	  secfile_lookup_str(file, "player%d.map_u%03d", plrno, nat_y),
	  set_savegame_special(&map_get_player_tile(ptile, plr)->special,
			       ch, default_specials + 4));
      LOAD_MAP_DATA(ch, nat_y, ptile,
	  secfile_lookup_str_default (file, NULL, "player%d.map_n%03d",
				      plrno, nat_y),
	  set_savegame_special(&map_get_player_tile(ptile, plr)->special,
			       ch, default_specials + 8));
      LOAD_MAP_DATA(ch, nat_y, ptile,
	secfile_lookup_str(file, "map.l%03d", nat_y),
	set_savegame_old_resource(&map_get_player_tile(ptile, plr)->resource,
				  ptile->terrain, ch, 0));
      LOAD_MAP_DATA(ch, nat_y, ptile,
	secfile_lookup_str(file, "map.n%03d", nat_y),
	set_savegame_old_resource(&map_get_player_tile(ptile, plr)->resource,
				  ptile->terrain, ch, 1));
    }

    /* get 4-bit segments of 16-bit "updated" field */
    LOAD_MAP_DATA(ch, nat_y, ptile,
		  secfile_lookup_str
		  (file, "player%d.map_ua%03d", plrno, nat_y),
		  map_get_player_tile(ptile, plr)->last_updated =
		  ascii_hex2bin(ch, 0));
    LOAD_MAP_DATA(ch, nat_y, ptile,
		  secfile_lookup_str
		  (file, "player%d.map_ub%03d", plrno, nat_y),
		  map_get_player_tile(ptile, plr)->last_updated |=
		  ascii_hex2bin(ch, 1));
    LOAD_MAP_DATA(ch, nat_y, ptile,
		  secfile_lookup_str
		  (file, "player%d.map_uc%03d", plrno, nat_y),
		  map_get_player_tile(ptile, plr)->last_updated |=
		  ascii_hex2bin(ch, 2));
    LOAD_MAP_DATA(ch, nat_y, ptile,
		  secfile_lookup_str
		  (file, "player%d.map_ud%03d", plrno, nat_y),
		  map_get_player_tile(ptile, plr)->last_updated |=
		  ascii_hex2bin(ch, 3));

    {
      int j;
      struct dumb_city *pdcity;
      i = secfile_lookup_int(file, "player%d.total_ncities", plrno);
      for (j = 0; j < i; j++) {
	int nat_x, nat_y;
	struct tile *ptile;
	int k, id;
	const char *p;

	nat_x = secfile_lookup_int(file, "player%d.dc%d.x", plrno, j);
	nat_y = secfile_lookup_int(file, "player%d.dc%d.y", plrno, j);
	ptile = native_pos_to_tile(nat_x, nat_y);

	pdcity = fc_malloc(sizeof(*pdcity));
	pdcity->id = secfile_lookup_int(file, "player%d.dc%d.id", plrno, j);
	sz_strlcpy(pdcity->name, secfile_lookup_str(file, "player%d.dc%d.name", plrno, j));
	pdcity->size = secfile_lookup_int(file, "player%d.dc%d.size", plrno, j);
	pdcity->occupied = secfile_lookup_bool_default(file, FALSE,
					"player%d.dc%d.occupied", plrno, j);
        pdcity->walls = secfile_lookup_bool_default(file, FALSE,
                                        "player%d.dc%d.walls", plrno, j);
	pdcity->happy = secfile_lookup_bool_default(file, FALSE,
					"player%d.dc%d.happy", plrno, j);
	pdcity->unhappy = secfile_lookup_bool_default(file, FALSE,
					"player%d.dc%d.unhappy", plrno, j);
	id = secfile_lookup_int(file, "player%d.dc%d.owner", plrno, j);
	pdcity->owner = get_player(id);

	/* Initialise list of improvements */
	BV_CLR_ALL(pdcity->improvements);

	p = secfile_lookup_str_default(file, NULL,
				       "player%d.dc%d.improvements", plrno, j);
	if (!p) {
	  /* old savegames */
	} else {
	  for (k = 0; k < improvement_order_size && p[k]; k++) {
	    if (p[k] == '1') {
	      id = find_improvement_by_name_orig(improvement_order[k]);
	      if (id != -1) {
		BV_SET(pdcity->improvements, id);
	      }
	    }
	  }
	}

	map_get_player_tile(ptile, plr)->city = pdcity;
	alloc_id(pdcity->id);
      }
    }

    /* This shouldn't be neccesary if the savegame was consistent, but there
       is a bug in some pre-1.11 savegames. Anyway, it can't hurt */
    whole_map_iterate(ptile) {
      if (map_is_known_and_seen(ptile, plr, V_MAIN)) {
	update_player_tile_knowledge(plr, ptile);
	reality_check_city(plr, ptile);
	if (tile_get_city(ptile)) {
	  update_dumb_city(plr, tile_get_city(ptile));
	}
      }
    } whole_map_iterate_end;

  } else {
    /* We have an old savegame or fog of war was turned off; the
       players private knowledge is set to be what he could see
       without fog of war */
    whole_map_iterate(ptile) {
      if (map_is_known(ptile, plr)) {
	struct city *pcity = tile_get_city(ptile);
	update_player_tile_last_seen(plr, ptile);
	update_player_tile_knowledge(plr, ptile);
	if (pcity)
	  update_dumb_city(plr, pcity);
      }
    } whole_map_iterate_end;
  }
}

/***************************************************************
...
***************************************************************/
static void player_save(struct player *plr, int plrno,
			struct section_file *file)
{
  int i;
  char invs[A_LAST+1];
  struct player_spaceship *ship = &plr->spaceship;
  struct ai_data *ai = ai_data_get(plr);
  int wlist_max_length = 0;

  secfile_insert_str(file, plr->name, "player%d.name", plrno);
  secfile_insert_str(file, plr->username, "player%d.username", plrno);
  secfile_insert_str(file, plr->ranked_username, "player%d.ranked_username",
                     plrno);
  secfile_insert_str(file, get_nation_name_orig(plr->nation),
		     "player%d.nation", plrno);
  /* 1.15 and later won't use the race field, they key on the nation string 
   * This field is kept only for forward compatibility
   * Nations can't be saved correctly because race must be < 62 */
  secfile_insert_int(file, plrno, "player%d.race", plrno);

  secfile_insert_int(file, plr->team ? plr->team->index : -1,
		     "player%d.team_no", plrno);

  secfile_insert_str(file, plr->government->name_orig,
		     "player%d.government_name", plrno);
  /* 1.15 and later won't use "government" field; it's kept for forward 
   * compatibility */
  secfile_insert_int(file, old_government_id(plr->government),
                     "player%d.government", plrno);

  if (plr->target_government) {
    secfile_insert_str(file, plr->target_government->name_orig,
		       "player%d.target_government_name", plrno);
  }

  players_iterate(pother) {
    secfile_insert_bool(file, BV_ISSET(plr->embassy, pother->player_no),
			"player%d.embassy%d", plrno, pother->player_no);
  } players_iterate_end;

  /* Required for 2.0 and earlier servers.  Remove eventually. */
  secfile_insert_int(file, 0, "player%d.embassy", plrno);

  /* This field won't be used; it's kept only for forward compatibility. 
   * City styles are specified by name since CVS 12/01-04. */
  secfile_insert_int(file, 0, "player%d.city_style", plrno);

  /* This is the new city style field to be used */
  secfile_insert_str(file, get_city_style_name_orig(plr->city_style),
                      "player%d.city_style_by_name", plrno);

  secfile_insert_bool(file, plr->is_male, "player%d.is_male", plrno);
  secfile_insert_bool(file, plr->is_alive, "player%d.is_alive", plrno);
  secfile_insert_bool(file, plr->ai.control, "player%d.ai.control", plrno);
  for (i = 0; i < MAX_NUM_PLAYERS; i++) {
    secfile_insert_int(file, plr->ai.love[i],
                       "player%d.ai%d.love", plrno, i);
    secfile_insert_int(file, ai->diplomacy.player_intel[i].spam, 
                       "player%d.ai%d.spam", plrno, i);
    secfile_insert_int(file, ai->diplomacy.player_intel[i].countdown, 
                       "player%d.ai%d.countdown", plrno, i);
    secfile_insert_int(file, ai->diplomacy.player_intel[i].war_reason, 
                       "player%d.ai%d.war_reason", plrno, i);
    secfile_insert_int(file, ai->diplomacy.player_intel[i].ally_patience, 
                       "player%d.ai%d.patience", plrno, i);
    secfile_insert_int(file, ai->diplomacy.player_intel[i].warned_about_space, 
                       "player%d.ai%d.warn_space", plrno, i);
    secfile_insert_int(file, ai->diplomacy.player_intel[i].asked_about_peace, 
                       "player%d.ai%d.ask_peace", plrno, i);
    secfile_insert_int(file, ai->diplomacy.player_intel[i].asked_about_alliance, 
                       "player%d.ai%d.ask_alliance", plrno, i);
    secfile_insert_int(file, ai->diplomacy.player_intel[i].asked_about_ceasefire, 
                       "player%d.ai%d.ask_ceasefire", plrno, i);
  }
  save_technology(file, "player%d.ai.tech_goal",
		  plrno, get_player_research(plr)->tech_goal);
  secfile_insert_int(file, plr->ai.skill_level,
		     "player%d.ai.skill_level", plrno);
  secfile_insert_int(file, plr->ai.barbarian_type, "player%d.ai.is_barbarian", plrno);
  secfile_insert_int(file, plr->economic.gold, "player%d.gold", plrno);
  secfile_insert_int(file, plr->economic.tax, "player%d.tax", plrno);
  secfile_insert_int(file, plr->economic.science, "player%d.science", plrno);
  secfile_insert_int(file, plr->economic.luxury, "player%d.luxury", plrno);
  
  secfile_insert_int(file, plr->bulbs_last_turn, "player%d.bulbs_last_turn",
                     plrno);

  secfile_insert_int(file, get_player_research(plr)->future_tech,
		     "player%d.futuretech", plrno);

  secfile_insert_int(file, get_player_research(plr)->bulbs_researched, 
		     "player%d.researched", plrno);
  secfile_insert_int(file, get_player_research(plr)->bulbs_researched_before,
		     "player%d.researched_before", plrno);
  secfile_insert_bool(file, get_player_research(plr)->got_tech,
		      "player%d.research_got_tech", plrno);
  save_technology(file, "player%d.research_changed_from", plrno, 
                  get_player_research(plr)->changed_from);
  secfile_insert_int(file, get_player_research(plr)->techs_researched,
		     "player%d.researchpoints", plrno);
  save_technology(file, "player%d.researching", plrno,
                  get_player_research(plr)->researching);  

  secfile_insert_bool(file, plr->capital, "player%d.capital", plrno);

  secfile_insert_int(file, plr->revolution_finishes,
		     "player%d.revolution_finishes", plrno);
  {
    /* Insert the old-style "revolution" value, for forward compatibility.
     * See the loading code for more explanation. */
    int revolution;

    if (plr->revolution_finishes < 0) {
      /* No revolution. */
      revolution = 0;
    } else if (plr->revolution_finishes <= game.info.turn) {
      revolution = 1; /* Approximation. */
    } else {
      revolution = plr->revolution_finishes - game.info.turn;
    }
    secfile_insert_int(file, revolution, "player%d.revolution", plrno);
  }

  /* 1.14 servers depend on technology order in ruleset. Here we are trying
   * to simulate 1.14.1 default order */
  init_old_technology_bitvector(invs);
  tech_type_iterate(tech_id) {
    if (get_invention(plr, tech_id) == TECH_KNOWN) {
      add_technology_into_old_bitvector(invs, tech_id);
    }
  } tech_type_iterate_end;
  secfile_insert_str(file, invs, "player%d.invs", plrno);
  
  /* Save technology lists as bitvector. Note that technology order is 
   * saved in savefile.technology_order */
  tech_type_iterate(tech_id) {
    invs[tech_id] = (get_invention(plr, tech_id) == TECH_KNOWN) ? '1' : '0';
  } tech_type_iterate_end;
  invs[game.control.num_tech_types] = '\0';
  secfile_insert_str(file, invs, "player%d.invs_new", plrno);

  for (i = 0; i < MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
    secfile_insert_int(file, plr->diplstates[i].type,
		       "player%d.diplstate%d.type", plrno, i);
    secfile_insert_int(file, plr->diplstates[i].max_state,
		       "player%d.diplstate%d.max_state", plrno, i);
    secfile_insert_int(file, plr->diplstates[i].first_contact_turn,
		       "player%d.diplstate%d.first_contact_turn", plrno, i);
    secfile_insert_int(file, plr->diplstates[i].turns_left,
		       "player%d.diplstate%d.turns_left", plrno, i);
    secfile_insert_int(file, plr->diplstates[i].has_reason_to_cancel,
		       "player%d.diplstate%d.has_reason_to_cancel", plrno, i);
    secfile_insert_int(file, plr->diplstates[i].contact_turns_left,
		       "player%d.diplstate%d.contact_turns_left", plrno, i);
  }

  {
    char vision[MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS+1];

    for (i=0; i < MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++)
      vision[i] = gives_shared_vision(plr, get_player(i)) ? '1' : '0';
    vision[i] = '\0';
    secfile_insert_str(file, vision, "player%d.gives_shared_vision", plrno);
  }

  secfile_insert_int(file, ship->state, "player%d.spaceship.state", plrno);

  if (ship->state != SSHIP_NONE) {
    char prefix[32];
    char st[NUM_SS_STRUCTURALS+1];
    int i;
    
    my_snprintf(prefix, sizeof(prefix), "player%d.spaceship", plrno);

    secfile_insert_int(file, ship->structurals, "%s.structurals", prefix);
    secfile_insert_int(file, ship->components, "%s.components", prefix);
    secfile_insert_int(file, ship->modules, "%s.modules", prefix);
    secfile_insert_int(file, ship->fuel, "%s.fuel", prefix);
    secfile_insert_int(file, ship->propulsion, "%s.propulsion", prefix);
    secfile_insert_int(file, ship->habitation, "%s.habitation", prefix);
    secfile_insert_int(file, ship->life_support, "%s.life_support", prefix);
    secfile_insert_int(file, ship->solar_panels, "%s.solar_panels", prefix);
    
    for(i=0; i<NUM_SS_STRUCTURALS; i++) {
      st[i] = (ship->structure[i]) ? '1' : '0';
    }
    st[i] = '\0';
    secfile_insert_str(file, st, "%s.structure", prefix);
    if (ship->state >= SSHIP_LAUNCHED) {
      secfile_insert_int(file, ship->launch_year, "%s.launch_year", prefix);
    }
  }

  secfile_insert_int(file, unit_list_size(plr->units), "player%d.nunits", 
		     plrno);
  secfile_insert_int(file, city_list_size(plr->cities), "player%d.ncities", 
		     plrno);

  i = -1;
  unit_list_iterate(plr->units, punit) {
    i++;
    int activity;

    secfile_insert_int(file, punit->id, "player%d.u%d.id", plrno, i);
    secfile_insert_int(file, punit->tile->nat_x, "player%d.u%d.x", plrno, i);
    secfile_insert_int(file, punit->tile->nat_y, "player%d.u%d.y", plrno, i);
    secfile_insert_int(file, punit->veteran, "player%d.u%d.veteran", 
				plrno, i);
    secfile_insert_bool(file, punit->foul, "player%d.u%d.foul", 
				plrno, i);
    secfile_insert_int(file, punit->hp, "player%d.u%d.hp", plrno, i);
    secfile_insert_int(file, punit->homecity, "player%d.u%d.homecity",
				plrno, i);
    /* .type is actually kept only for forward compatibility */
    secfile_insert_int(file, old_unit_type_id(punit->type),
		       "player%d.u%d.type",
		       plrno, i);
    secfile_insert_str(file, unit_name_orig(punit->type),
		       "player%d.u%d.type_by_name",
		       plrno, i);

    activity = punit->activity;

    if (activity == ACTIVITY_BASE) {
      struct base_type *pbase;
      pbase = base_type_get_by_id(punit->activity_base);

      if (pbase->gui_type == BASE_GUI_FORTRESS) {
        activity = ACTIVITY_FORTRESS;
      } else if (pbase->gui_type == BASE_GUI_AIRBASE) {
        activity = ACTIVITY_AIRBASE;
      } else {
        /* Gui type other. Make sensible fallback */
        activity = ACTIVITY_FORTRESS;
      }
    }
    secfile_insert_int(file, activity, "player%d.u%d.activity",
                       plrno, i);
    secfile_insert_int(file, punit->activity_count, 
				"player%d.u%d.activity_count",
				plrno, i);
    secfile_insert_int(file, punit->activity_target, 
				"player%d.u%d.activity_target",
				plrno, i);
    secfile_insert_bool(file, punit->done_moving,
			"player%d.u%d.done_moving", plrno, i);
    secfile_insert_int(file, punit->moves_left, "player%d.u%d.moves",
		                plrno, i);
    secfile_insert_int(file, punit->fuel, "player%d.u%d.fuel",
		                plrno, i);
    secfile_insert_int(file, punit->birth_turn, "player%d.u%d.born",
                       plrno, i);
    secfile_insert_int(file, punit->battlegroup,
		       "player%d.u%d.battlegroup", plrno, i);

    if (punit->goto_tile) {
      secfile_insert_bool(file, TRUE, "player%d.u%d.go", plrno, i);
      secfile_insert_int(file, punit->goto_tile->nat_x,
			 "player%d.u%d.goto_x", plrno, i);
      secfile_insert_int(file, punit->goto_tile->nat_y,
			 "player%d.u%d.goto_y", plrno, i);
    } else {
      secfile_insert_bool(file, FALSE, "player%d.u%d.go", plrno, i);
      /* for compatibility with older servers */
      secfile_insert_int(file, 0, "player%d.u%d.goto_x", plrno, i);
      secfile_insert_int(file, 0, "player%d.u%d.goto_y", plrno, i);
    }

    secfile_insert_bool(file, punit->ai.control, "player%d.u%d.ai", plrno, i);
    secfile_insert_int(file, punit->ai.passenger, "player%d.u%d.passenger", 
                       plrno, i);
    secfile_insert_int(file, punit->ai.ferryboat, "player%d.u%d.ferryboat", 
                       plrno, i);
    secfile_insert_int(file, punit->ai.charge, "player%d.u%d.charge", plrno, i);
    secfile_insert_int(file, punit->ai.bodyguard, "player%d.u%d.bodyguard", 
                       plrno, i);
    secfile_insert_int(file, punit->ord_map, "player%d.u%d.ord_map", plrno, i);
    secfile_insert_int(file, punit->ord_city, "player%d.u%d.ord_city", plrno, i);
    secfile_insert_bool(file, punit->moved, "player%d.u%d.moved", plrno, i);
    secfile_insert_bool(file, punit->paradropped, "player%d.u%d.paradropped", plrno, i);
    secfile_insert_int(file, punit->transported_by,
		       "player%d.u%d.transported_by", plrno, i);
    if (punit->has_orders) {
      int len = punit->orders.length, j;
      char orders_buf[len + 1], dir_buf[len + 1], act_buf[len + 1];

      secfile_insert_int(file, len, "player%d.u%d.orders_length", plrno, i);
      secfile_insert_int(file, punit->orders.index,
			 "player%d.u%d.orders_index", plrno, i);
      secfile_insert_bool(file, punit->orders.repeat,
			  "player%d.u%d.orders_repeat", plrno, i);
      secfile_insert_bool(file, punit->orders.vigilant,
			  "player%d.u%d.orders_vigilant", plrno, i);

      for (j = 0; j < len; j++) {
	orders_buf[j] = order2char(punit->orders.list[j].order);
	dir_buf[j] = '?';
	act_buf[j] = '?';
	switch (punit->orders.list[j].order) {
	case ORDER_MOVE:
	  dir_buf[j] = dir2char(punit->orders.list[j].dir);
	  break;
	case ORDER_ACTIVITY:
          if (punit->orders.list[j].activity == ACTIVITY_BASE) {
            struct base_type *pbase;
            pbase = base_type_get_by_id(punit->orders.list[j].base);

            if (pbase->gui_type == BASE_GUI_FORTRESS) {
              act_buf[j] = activity2char(ACTIVITY_FORTRESS);
            } else if (pbase->gui_type == BASE_GUI_AIRBASE) {
              act_buf[j] = activity2char(ACTIVITY_AIRBASE);
            } else {
              /* Saving others as fortress */
              act_buf[j] = activity2char(ACTIVITY_FORTRESS);
            }
          } else {
            act_buf[j] = activity2char(punit->orders.list[j].activity);
          }
	  break;
	case ORDER_FULL_MP:
	case ORDER_BUILD_CITY:
	case ORDER_DISBAND:
	case ORDER_BUILD_WONDER:
	case ORDER_TRADEROUTE:
	case ORDER_HOMECITY:
	case ORDER_LAST:
	  break;
	}
      }
      orders_buf[len] = dir_buf[len] = act_buf[len] = '\0';

      secfile_insert_str(file, orders_buf,
			 "player%d.u%d.orders_list", plrno, i);
      secfile_insert_str(file, dir_buf,
			 "player%d.u%d.dir_list", plrno, i);
      secfile_insert_str(file, act_buf,
			 "player%d.u%d.activity_list", plrno, i);
    } else {
      /* Put all the same fields into the savegame - otherwise the
       * registry code can't correctly use a tabular format and the
       * savegame will be bigger. */
      secfile_insert_int(file, 0, "player%d.u%d.orders_length", plrno, i);
      secfile_insert_int(file, 0, "player%d.u%d.orders_index", plrno, i);
      secfile_insert_bool(file, FALSE,
			  "player%d.u%d.orders_repeat", plrno, i);
      secfile_insert_bool(file, FALSE,
			  "player%d.u%d.orders_vigilant", plrno, i);
      secfile_insert_str(file, "-",
			 "player%d.u%d.orders_list", plrno, i);
      secfile_insert_str(file, "-",
			 "player%d.u%d.dir_list", plrno, i);
      secfile_insert_str(file, "-",
			 "player%d.u%d.activity_list", plrno, i);
    }
  }
  unit_list_iterate_end;

  i = -1;

  /* First determine lenght of longest worklist */
  city_list_iterate(plr->cities, pcity) {
    if (pcity->worklist.length > wlist_max_length) {
      wlist_max_length = pcity->worklist.length;
    }
  } city_list_iterate_end;

  city_list_iterate(plr->cities, pcity) {
    int j, x, y;
    char citymap_buf[CITY_MAP_SIZE * CITY_MAP_SIZE + 1];
    char impr_buf[MAX_NUM_ITEMS + 1];

    i++;
    secfile_insert_int(file, pcity->id, "player%d.c%d.id", plrno, i);
    secfile_insert_int(file, pcity->tile->nat_x, "player%d.c%d.x", plrno, i);
    secfile_insert_int(file, pcity->tile->nat_y, "player%d.c%d.y", plrno, i);
    secfile_insert_str(file, pcity->name, "player%d.c%d.name", plrno, i);
    secfile_insert_int(file, pcity->original->player_no,
		       "player%d.c%d.original", plrno, i);
    secfile_insert_int(file, pcity->size, "player%d.c%d.size", plrno, i);
    secfile_insert_int(file, pcity->steal, "player%d.c%d.steal", plrno, i);
    specialist_type_iterate(sp) {
      secfile_insert_int(file, pcity->specialists[sp],
			 "player%d.c%d.n%s", plrno, i,
			 get_specialist(sp)->name);
    } specialist_type_iterate_end;

    for (j = 0; j < NUM_TRADEROUTES; j++)
      secfile_insert_int(file, pcity->trade[j], "player%d.c%d.traderoute%d", 
			 plrno, i, j);
    
    secfile_insert_int(file, pcity->food_stock, "player%d.c%d.food_stock", 
		       plrno, i);
    secfile_insert_int(file, pcity->shield_stock, "player%d.c%d.shield_stock", 
		       plrno, i);
    secfile_insert_int(file, pcity->turn_last_built,
		       "player%d.c%d.turn_last_built", plrno, i);
    secfile_insert_bool(file, pcity->changed_from.is_unit,
		       "player%d.c%d.changed_from_is_unit", plrno, i);
    if (pcity->changed_from.is_unit) {
      struct unit_type *punittype = get_unit_type(pcity->changed_from.value);

      secfile_insert_int(file, old_unit_type_id(punittype),
		         "player%d.c%d.changed_from_id", plrno, i);
      secfile_insert_str(file, unit_name_orig(punittype),
                         "player%d.c%d.changed_from_name", plrno, i);
    } else {
      secfile_insert_int(file, old_impr_type_id(pcity->changed_from.value),
		         "player%d.c%d.changed_from_id", plrno, i);    
      secfile_insert_str(file, get_improvement_name_orig(
                                 pcity->changed_from.value),
                         "player%d.c%d.changed_from_name", plrno, i);
    }

    secfile_insert_int(file, pcity->before_change_shields,
		       "player%d.c%d.before_change_shields", plrno, i);
    secfile_insert_int(file, pcity->disbanded_shields,
		       "player%d.c%d.disbanded_shields", plrno, i);
    secfile_insert_int(file, pcity->caravan_shields,
		       "player%d.c%d.caravan_shields", plrno, i);
    secfile_insert_int(file, pcity->last_turns_shield_surplus,
		       "player%d.c%d.last_turns_shield_surplus", plrno, i);

    secfile_insert_int(file, pcity->anarchy, "player%d.c%d.anarchy", plrno,i);
    secfile_insert_int(file, pcity->rapture, "player%d.c%d.rapture", plrno,i);
    secfile_insert_bool(file, pcity->was_happy, "player%d.c%d.was_happy", plrno,i);
    if (pcity->turn_founded == game.info.turn) {
      j = -1;
    } else {
      assert(pcity->did_buy == TRUE || pcity->did_buy == FALSE);
      j = pcity->did_buy ? 1 : 0;
    }
    secfile_insert_int(file, j, "player%d.c%d.did_buy", plrno, i);
    secfile_insert_int(file, pcity->turn_founded,
		       "player%d.c%d.turn_founded", plrno, i);
    secfile_insert_bool(file, pcity->did_sell, "player%d.c%d.did_sell", plrno,i);
    secfile_insert_bool(file, pcity->airlift, "player%d.c%d.airlift", plrno,i);

    /* New options format as of Freeciv 2.1. */
    for (j = 0; j < CITYO_LAST; j++) {
      secfile_insert_bool(file, BV_ISSET(pcity->city_options, j),
			  "player%d.c%d.option%d", plrno, i, j);
    }

    j=0;
    for(y=0; y<CITY_MAP_SIZE; y++) {
      for(x=0; x<CITY_MAP_SIZE; x++) {
	switch (get_worker_city(pcity, x, y)) {
	case C_TILE_EMPTY:
	  citymap_buf[j++] = '0';
	  break;
	case C_TILE_WORKER:
	  citymap_buf[j++] = '1';
	  break;
	case C_TILE_UNAVAILABLE:
	  citymap_buf[j++] = '2';
	  break;
	default:
	  citymap_buf[j++] = '?';
	  assert(0);
	  break;
	}
      }
    }
    citymap_buf[j]='\0';
    assert(j < ARRAY_SIZE(citymap_buf));
    secfile_insert_str(file, citymap_buf, "player%d.c%d.workers", plrno, i);

    secfile_insert_bool(file, pcity->production.is_unit, 
		       "player%d.c%d.is_building_unit", plrno, i);
    if (pcity->production.is_unit) {
      struct unit_type *punittype = get_unit_type(pcity->production.value);
      secfile_insert_int(file, old_unit_type_id(punittype),
		         "player%d.c%d.currently_building", plrno, i);
      secfile_insert_str(file, unit_name_orig(punittype),
                         "player%d.c%d.currently_building_name", plrno, i);
    } else {
      secfile_insert_int(file, old_impr_type_id(pcity->production.value),
                         "player%d.c%d.currently_building", plrno, i);
      secfile_insert_str(file, get_improvement_name_orig(
                                   pcity->production.value),
                         "player%d.c%d.currently_building_name", plrno, i);
    }

    /* 1.14 servers depend on improvement order in ruleset. Here we
     * are trying to simulate 1.14.1 default order
     */
    init_old_improvement_bitvector(impr_buf);
    impr_type_iterate(id) {
      if (pcity->improvements[id] != I_NONE) {
        add_improvement_into_old_bitvector(impr_buf, id);
      }
    } impr_type_iterate_end;
    assert(strlen(impr_buf) < sizeof(impr_buf));
    secfile_insert_str(file, impr_buf, "player%d.c%d.improvements", plrno, i);

    /* Save improvement list as bitvector. Note that improvement order
     * is saved in savefile.improvement_order.
     */
    impr_type_iterate(id) {
      impr_buf[id] = (pcity->improvements[id] != I_NONE) ? '1' : '0';
    } impr_type_iterate_end;
    impr_buf[game.control.num_impr_types] = '\0';
    assert(strlen(impr_buf) < sizeof(impr_buf));
    secfile_insert_str(file, impr_buf,
		       "player%d.c%d.improvements_new", plrno, i);    

    worklist_save(file, &pcity->worklist, wlist_max_length,
                  "player%d.c%d", plrno, i);

    /* FIXME: remove this when the urgency is properly recalculated. */
    secfile_insert_int(file, pcity->ai.urgency,
		       "player%d.c%d.ai.urgency", plrno, i);
  }
  city_list_iterate_end;

  /********** Put the players private map **********/
 /* Otherwise the player can see all, and there's no reason to save the private map. */
  if (game.info.fogofwar
      && game.save_options.save_private_map) {

    /* put the terrain type */
    SAVE_PLAYER_MAP_DATA(ptile, file,"player%d.map_t%03d", plrno, 
			 terrain2char(map_get_player_tile
				      (ptile, plr)->terrain));
    SAVE_PLAYER_MAP_DATA(ptile, file, "player%d.map_res%03d", plrno,
	resource_to_identifier(map_get_player_tile(ptile, plr)->resource));

    special_halfbyte_iterate(j) {
      char buf[32]; /* enough for sprintf() below */
      enum tile_special_type mod[4];
      int l;

      for (l = 0; l < 4; l++) {
	mod[l] = MIN(4 * j + l, S_LAST);
      }
      sprintf (buf, "player%%d.map_spe%02d_%%03d", j);
      SAVE_PLAYER_MAP_DATA(ptile, file, buf, plrno,
	get_savegame_special(map_get_player_tile(ptile, plr)->special, mod));
    } special_halfbyte_iterate_end;

    /* put 4-bit segments of 16-bit "updated" field */
    SAVE_PLAYER_MAP_DATA(ptile, file,"player%d.map_ua%03d", plrno,
			 bin2ascii_hex(map_get_player_tile
				       (ptile, plr)->last_updated, 0));
    SAVE_PLAYER_MAP_DATA(ptile, file, "player%d.map_ub%03d", plrno,
			 bin2ascii_hex(map_get_player_tile
				       (ptile, plr)->last_updated, 1));
    SAVE_PLAYER_MAP_DATA(ptile, file,"player%d.map_uc%03d", plrno,
			 bin2ascii_hex(map_get_player_tile
				       (ptile, plr)->last_updated, 2));
    SAVE_PLAYER_MAP_DATA(ptile, file, "player%d.map_ud%03d", plrno,
			 bin2ascii_hex(map_get_player_tile
				       (ptile, plr)->last_updated, 3));

    if (TRUE) {
      struct dumb_city *pdcity;
      char impr_buf[MAX_NUM_ITEMS + 1];

      i = 0;
      
      whole_map_iterate(ptile) {
	if ((pdcity = map_get_player_tile(ptile, plr)->city)) {
	  secfile_insert_int(file, pdcity->id, "player%d.dc%d.id", plrno,
			     i);
	  secfile_insert_int(file, ptile->nat_x,
			     "player%d.dc%d.x", plrno, i);
	  secfile_insert_int(file, ptile->nat_y,
			     "player%d.dc%d.y", plrno, i);
	  secfile_insert_str(file, pdcity->name, "player%d.dc%d.name",
			     plrno, i);
	  secfile_insert_int(file, pdcity->size, "player%d.dc%d.size",
			     plrno, i);
	  secfile_insert_bool(file, FALSE,
			      "player%d.dc%d.has_walls", plrno, i);
	  secfile_insert_bool(file, pdcity->occupied,
			      "player%d.dc%d.occupied", plrno, i);
          secfile_insert_bool(file, pdcity->walls,
                              "player%d.dc%d.walls", plrno, i);
	  secfile_insert_bool(file, pdcity->happy,
			      "player%d.dc%d.happy", plrno, i);
	  secfile_insert_bool(file, pdcity->unhappy,
			      "player%d.dc%d.unhappy", plrno, i);
	  secfile_insert_int(file, pdcity->owner->player_no,
			     "player%d.dc%d.owner", plrno, i);

	  /* Save improvement list as bitvector. Note that improvement order
	   * is saved in savefile.improvement_order.
	   */
	  impr_type_iterate(id) {
	    impr_buf[id] = BV_ISSET(pdcity->improvements, id) ? '1' : '0';
	  } impr_type_iterate_end;
	  impr_buf[game.control.num_impr_types] = '\0';
	  assert(strlen(impr_buf) < sizeof(impr_buf));
	  secfile_insert_str(file, impr_buf,
			     "player%d.dc%d.improvements", plrno, i);    

	  i++;
	}
      } whole_map_iterate_end;
    }
    secfile_insert_int(file, i, "player%d.total_ncities", plrno);
  }

#define PART_SIZE (2*1024)
  if (plr->attribute_block.data) {
    char *quoted = quote_block(plr->attribute_block.data,
			       plr->attribute_block.length);
    char part[PART_SIZE + 1];
    int current_part_nr, parts;
    size_t bytes_left;

    secfile_insert_int(file, plr->attribute_block.length,
		       "player%d.attribute_v2_block_length", plrno);
    secfile_insert_int(file, strlen(quoted),
		       "player%d.attribute_v2_block_length_quoted", plrno);

    parts = (strlen(quoted) - 1) / PART_SIZE + 1;
    bytes_left = strlen(quoted);

    secfile_insert_int(file, parts,
		       "player%d.attribute_v2_block_parts", plrno);

    for (current_part_nr = 0; current_part_nr < parts; current_part_nr++) {
      size_t size_of_current_part = MIN(bytes_left, PART_SIZE);

      assert(bytes_left);

      memcpy(part, quoted + PART_SIZE * current_part_nr,
	     size_of_current_part);
      part[size_of_current_part] = 0;
      secfile_insert_str(file, part,
			 "player%d.attribute_v2_block_data.part%d", plrno,
			 current_part_nr);
      bytes_left -= size_of_current_part;
    }
    assert(bytes_left == 0);
    free(quoted);
  }
}


/***************************************************************
 Assign values to ord_city and ord_map for each unit, so the
 values can be saved.
***************************************************************/
static void calc_unit_ordering(void)
{
  int j;

  players_iterate(pplayer) {
    /* to avoid junk values for unsupported units: */
    unit_list_iterate(pplayer->units, punit) {
      punit->ord_city = 0;
    } unit_list_iterate_end;
    city_list_iterate(pplayer->cities, pcity) {
      j = 0;
      unit_list_iterate(pcity->units_supported, punit) {
	punit->ord_city = j++;
      } unit_list_iterate_end;
    } city_list_iterate_end;
  } players_iterate_end;

  whole_map_iterate(ptile) {
    j = 0;
    unit_list_iterate(ptile->units, punit) {
      punit->ord_map = j++;
    } unit_list_iterate_end;
  } whole_map_iterate_end;
}

/***************************************************************
 For each city and tile, sort unit lists according to
 ord_city and ord_map values.
***************************************************************/
static void apply_unit_ordering(void)
{
  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      unit_list_sort_ord_city(pcity->units_supported);
    }
    city_list_iterate_end;
  } players_iterate_end;

  whole_map_iterate(ptile) {
    unit_list_sort_ord_map(ptile->units);
  } whole_map_iterate_end;
}

/***************************************************************
Old savegames have defects...
***************************************************************/
static void check_city(struct city *pcity)
{
  city_map_iterate(x, y) {
    bool res = city_can_work_tile(pcity, x, y);
    switch (pcity->city_map[x][y]) {
    case C_TILE_EMPTY:
      if (!res) {
	set_worker_city(pcity, x, y, C_TILE_UNAVAILABLE);
	freelog(LOG_DEBUG, "unavailable tile marked as empty!");
      }
      break;
    case C_TILE_WORKER:
      if (!res) {
	struct tile *ptile;

	pcity->specialists[DEFAULT_SPECIALIST]++;
	set_worker_city(pcity, x, y, C_TILE_UNAVAILABLE);
	freelog(LOG_DEBUG, "Worked tile was unavailable!");

	ptile = city_map_to_map(pcity, x, y);

	map_city_radius_iterate(ptile, tile2) {
	  struct city *pcity2 = tile_get_city(tile2);
	  if (pcity2)
	    check_city(pcity2);
	} map_city_radius_iterate_end;
      }
      break;
    case C_TILE_UNAVAILABLE:
      if (res) {
	set_worker_city(pcity, x, y, C_TILE_EMPTY);
	freelog(LOG_DEBUG, "Empty tile Marked as unavailable!");
      }
      break;
    }
  } city_map_iterate_end;

  city_refresh(pcity);
}

/***************************************************************
...
***************************************************************/
void game_load(struct section_file *file)
{
  int i, k, id;
  enum server_states tmp_server_state;
  char *savefile_options;
  const char *string;
  char** improvement_order = NULL;
  int improvement_order_size = 0;
  char** technology_order = NULL;
  enum tile_special_type *special_order = NULL;
  int technology_order_size = 0;
  const char* name;
  int civstyle = 0;

  game.version = secfile_lookup_int_default(file, 0, "game.version");
  tmp_server_state = (enum server_states)
    secfile_lookup_int_default(file, RUN_GAME_STATE, "game.server_state");

  savefile_options = secfile_lookup_str(file, "savefile.options");
  if (has_capability("improvement_order", savefile_options)) {
    improvement_order = secfile_lookup_str_vec(file, &improvement_order_size,
                                               "savefile.improvement_order");
  }
  if (has_capability("technology_order", savefile_options)) {
    technology_order = secfile_lookup_str_vec(file, &technology_order_size,
                                              "savefile.technology_order");
  }
  if (has_capability("resources", savefile_options)) {
    char **modname;
    int nmod;
    enum tile_special_type j;

    modname = secfile_lookup_str_vec(file, &nmod,
				     "savefile.specials");
    /* make sure that the size of the array is divisible by 4 */
    special_order = fc_calloc(nmod + (4 - (nmod % 4)),
			      sizeof(*special_order));
    for (j = 0; j < nmod; j++) {
      special_order[j] = get_special_by_name_orig(modname[j]);
    }
    free(modname);
    for (; j < S_LAST + (4 - (S_LAST % 4)); j++) {
      special_order[j] = S_LAST;
    }
  }

  /* we require at least version 1.9.0 */
  if (10900 > game.version) {
    freelog(LOG_FATAL,
	    _("Savegame too old, at least version 1.9.0 required."));
    exit(EXIT_FAILURE);
  }

  /* We don't need savefile.reason, but read it anyway to avoid
   * warnings about unread secfile entries. */
  secfile_lookup_str_default(file, "None", "savefile.reason");

  {
    set_meta_patches_string(secfile_lookup_str_default(file, 
                                                default_meta_patches_string(),
                                                "game.metapatches"));
    set_meta_message_string(secfile_lookup_str_default(file, 
                                                default_meta_message_string(),
                                                "game.metamessage"));

    sz_strlcpy(srvarg.metaserver_addr,
	       secfile_lookup_str_default(file, DEFAULT_META_SERVER_ADDR,
					  "game.metaserver"));

    game.info.gold          = secfile_lookup_int(file, "game.gold");
    game.info.tech          = secfile_lookup_int(file, "game.tech");
    game.info.skill_level   = secfile_lookup_int(file, "game.skill_level");
    if (game.info.skill_level==0)
      game.info.skill_level = GAME_OLD_DEFAULT_SKILL_LEVEL;

    game.info.timeout       = secfile_lookup_int(file, "game.timeout");
    game.timeoutint = secfile_lookup_int_default(file,
						 GAME_DEFAULT_TIMEOUTINT,
						 "game.timeoutint");
    game.timeoutintinc =
	secfile_lookup_int_default(file, GAME_DEFAULT_TIMEOUTINTINC,
				   "game.timeoutintinc");
    game.timeoutinc =
	secfile_lookup_int_default(file, GAME_DEFAULT_TIMEOUTINC,
				   "game.timeoutinc");
    game.timeoutincmult =
	secfile_lookup_int_default(file, GAME_DEFAULT_TIMEOUTINCMULT,
				   "game.timeoutincmult");
    game.timeoutcounter =
	secfile_lookup_int_default(file, 1, "game.timeoutcounter");

    game.timeoutaddenemymove
      = secfile_lookup_int_default(file, game.timeoutaddenemymove,
				   "game.timeoutaddenemymove");

    
    game.info.end_year      = secfile_lookup_int(file, "game.end_year");
    game.info.shieldbox
      = secfile_lookup_int_default(file, GAME_DEFAULT_SHIELDBOX,
				   "game.box_shield");
    game.info.sciencebox
      = secfile_lookup_int_default(file, 0, "game.box_science");
    if (game.info.sciencebox == 0) {
      /* Researchcost was used for 2.0 and earlier servers. */
      game.info.sciencebox
	= 5 * secfile_lookup_int_default(file, 0, "game.researchcost");
      if (game.info.sciencebox == 0) {
	/* With even earlier servers (?) techlevel was used for this info. */
	game.info.sciencebox = 5 * secfile_lookup_int(file, "game.techlevel");
      }
    }

    game.info.year          = secfile_lookup_int(file, "game.year");

    if (has_capability("turn", savefile_options)) {
      game.info.turn = secfile_lookup_int(file, "game.turn");
    } else {
      game.info.turn = -2;
    }
    game.info.simultaneous_phases
      = secfile_lookup_bool_default(file, TRUE,
				    "game.simultaneous_phases_now");
    game.simultaneous_phases_stored
      = secfile_lookup_bool_default(file, TRUE,
				    "game.simultaneous_phases_stored");

    game.info.min_players   = secfile_lookup_int(file, "game.min_players");
    game.info.max_players   = secfile_lookup_int(file, "game.max_players");

    game.info.heating = secfile_lookup_int_default(file, 0, "game.heating");
    game.info.globalwarming = secfile_lookup_int(file, "game.globalwarming");
    game.info.warminglevel  = secfile_lookup_int(file, "game.warminglevel");
    game.info.cooling = secfile_lookup_int_default(file, 0, "game.cooling");
    game.info.nuclearwinter = secfile_lookup_int_default(file, 0, "game.nuclearwinter");
    game.info.coolinglevel  = secfile_lookup_int_default(file, 8, "game.coolinglevel");
    game.info.notradesize   = secfile_lookup_int_default(file, 0, "game.notradesize");
    game.info.fulltradesize = secfile_lookup_int_default(file, 1, "game.fulltradesize");
    game.info.angrycitizen  = secfile_lookup_bool_default(file, FALSE, "game.angrycitizen");

    if (game.version >= 10100) {
      game.info.diplcost    = secfile_lookup_int(file, "game.diplcost");
      game.info.freecost    = secfile_lookup_int(file, "game.freecost");
      game.info.conquercost = secfile_lookup_int(file, "game.conquercost");
      game.info.foodbox
	= secfile_lookup_int_default(file, 0, "game.box_food");
      if (game.info.foodbox == 0) {
	/* foodbox was used for 2.0 and earlier servers. */
	game.info.foodbox = 10 * secfile_lookup_int(file, "game.foodbox");
      }
      game.info.techpenalty = secfile_lookup_int(file, "game.techpenalty");
      game.info.razechance  = secfile_lookup_int(file, "game.razechance");

      /* suppress warnings about unused entries in old savegames: */
      (void) section_file_lookup(file, "game.rail_food");
      (void) section_file_lookup(file, "game.rail_prod");
      (void) section_file_lookup(file, "game.rail_trade");
      (void) section_file_lookup(file, "game.farmfood");
    }
    if (game.version >= 10300) {
      civstyle = secfile_lookup_int_default(file, 2, "game.civstyle");
      game.info.save_nturns = secfile_lookup_int(file, "game.save_nturns");
    }

    game.info.citymindist  = secfile_lookup_int_default(file,
      GAME_DEFAULT_CITYMINDIST, "game.citymindist");

    game.info.rapturedelay  = secfile_lookup_int_default(file,
      GAME_DEFAULT_RAPTUREDELAY, "game.rapturedelay");

    /* National borders setting. */
    game.info.borders = secfile_lookup_int_default(file, 0, "game.borders");
    game.info.happyborders = secfile_lookup_bool_default(file, FALSE, 
						    "game.happyborders");

    /* Diplomacy. */
    game.info.diplomacy = secfile_lookup_int_default(file, GAME_DEFAULT_DIPLOMACY, 
                                                "game.diplomacy");

    sz_strlcpy(game.save_name,
	       secfile_lookup_str_default(file, GAME_DEFAULT_SAVE_NAME,
					  "game.save_name"));

    game.info.aifill = secfile_lookup_int_default(file, 0, "game.aifill");

    game.scorelog = secfile_lookup_bool_default(file, FALSE, "game.scorelog");
    sz_strlcpy(game.id, secfile_lookup_str_default(file, "", "game.id"));

    game.info.fogofwar = secfile_lookup_bool_default(file, FALSE, "game.fogofwar");
    game.fogofwar_old = game.info.fogofwar;
  
    game.info.civilwarsize =
      secfile_lookup_int_default(file, GAME_DEFAULT_CIVILWARSIZE,
				 "game.civilwarsize");
    game.info.contactturns =
      secfile_lookup_int_default(file, GAME_DEFAULT_CONTACTTURNS,
				 "game.contactturns");
  
    if(has_capability("diplchance_percent", savefile_options)) {
      game.info.diplchance = secfile_lookup_int_default(file, game.info.diplchance,
						   "game.diplchance");
    } else {
      game.info.diplchance = secfile_lookup_int_default(file, 3, /* old default */
						   "game.diplchance");
      if (game.info.diplchance < 2) {
	game.info.diplchance = GAME_MAX_DIPLCHANCE;
      } else if (game.info.diplchance > 10) {
	game.info.diplchance = GAME_MIN_DIPLCHANCE;
      } else {
	game.info.diplchance = 100 - (10 * (game.info.diplchance - 1));
      }
    }

    game.info.aqueductloss = secfile_lookup_int_default(file, game.info.aqueductloss,
						   "game.aqueductloss");
    game.info.killcitizen = secfile_lookup_int_default(file, game.info.killcitizen,
						  "game.killcitizen");
    game.info.savepalace = secfile_lookup_bool_default(file, game.info.savepalace,
						"game.savepalace");
    game.info.turnblock = secfile_lookup_bool_default(file, game.info.turnblock,
						"game.turnblock");
    game.info.fixedlength = secfile_lookup_bool_default(file, game.info.fixedlength,
						  "game.fixedlength");
    game.info.barbarianrate = secfile_lookup_int_default(file, game.info.barbarianrate,
						    "game.barbarians");
    game.info.onsetbarbarian = secfile_lookup_int_default(file, game.info.onsetbarbarian,
						     "game.onsetbarbs");
    game.info.revolution_length
      = secfile_lookup_int_default(file, game.info.revolution_length,
				   "game.revolen");
    game.info.nbarbarians = 0; /* counted in player_load for compatibility with 
			     1.10.0 */
    game.info.occupychance = secfile_lookup_int_default(file, game.info.occupychance,
						   "game.occupychance");
    game.info.autoattack = secfile_lookup_bool_default(file,
						  GAME_DEFAULT_AUTOATTACK,
                                                  "game.autoattack");
    game.seed = secfile_lookup_int_default(file, game.seed,
					   "game.randseed");
    game.info.allowed_city_names =
	secfile_lookup_int_default(file, game.info.allowed_city_names,
				   "game.allowed_city_names"); 

    if(civstyle == 1) {
      string = "civ1";
    } else {
      string = "default";
    }

    if (!has_capability("rulesetdir", savefile_options)) {
      char *str2, *str =
	  secfile_lookup_str_default(file, "default", "game.info.t.techs");

      if (strcmp("classic",
		 secfile_lookup_str_default(file, "default",
					    "game.info.t.terrain")) == 0) {
	freelog(LOG_FATAL, _("The savegame uses the classic terrain "
			     "ruleset which is no longer supported."));
	exit(EXIT_FAILURE);
      }


#define T(x) \
      str2 = secfile_lookup_str_default(file, "default", x); \
      if (strcmp(str, str2) != 0) { \
	freelog(LOG_NORMAL, _("Warning: Different rulesetdirs " \
			      "('%s' and '%s') are no longer supported. " \
			      "Using '%s'."), \
			      str, str2, str); \
      }

      T("game.info.t.units");
      T("game.info.t.buildings");
      T("game.info.t.terrain");
      T("game.info.t.governments");
      T("game.info.t.nations");
      T("game.info.t.cities");
      T("game.info.t.game");
#undef T

      sz_strlcpy(game.rulesetdir, str);
    } else {
      sz_strlcpy(game.rulesetdir, 
	       secfile_lookup_str_default(file, string,
					  "game.rulesetdir"));
    }

    sz_strlcpy(game.demography,
	       secfile_lookup_str_default(file, GAME_DEFAULT_DEMOGRAPHY,
					  "game.demography"));
    sz_strlcpy(game.allow_take,
	       secfile_lookup_str_default(file, GAME_DEFAULT_ALLOW_TAKE,
					  "game.allow_take"));

    game.info.spacerace = secfile_lookup_bool_default(file, game.info.spacerace,
						"game.spacerace");

    game.info.auto_ai_toggle = 
      secfile_lookup_bool_default(file, game.info.auto_ai_toggle, 
                                  "game.auto_ai_toggle");

    load_rulesets();
  }

  game.info.nplayers      = secfile_lookup_int(file, "game.nplayers");


  script_state_load(file);

  {
    if (game.version >= 10300) {
      {
	if (!has_capability("startunits", savefile_options)) {
	  int settlers = secfile_lookup_int(file, "game.settlers");
	  int explorer = secfile_lookup_int(file, "game.explorer");
	  int i;
	  for (i = 0; settlers > 0 && i < (MAX_LEN_STARTUNIT - 1) ; i++, settlers--) {
	    game.info.start_units[i] = 'c';
	  }
	  for (; explorer > 0 && i < (MAX_LEN_STARTUNIT - 1) ; i++, explorer--) {
	    game.info.start_units[i] = 'x';
	  }
	  game.info.start_units[i] = '\0';
	} else {
	  sz_strlcpy(game.info.start_units,
		     secfile_lookup_str_default(file,
						GAME_DEFAULT_START_UNITS,
						"game.start_units"));
	}
	game.info.dispersion =
	  secfile_lookup_int_default(file, GAME_DEFAULT_DISPERSION,
				     "game.dispersion");
      }

      map.topology_id = secfile_lookup_int_default(file, MAP_ORIGINAL_TOPO,
					           "map.topology_id");
      map.size = secfile_lookup_int_default(file, MAP_DEFAULT_SIZE,
                                            "map.size");
      map.riches = secfile_lookup_int(file, "map.riches");
      map.huts = secfile_lookup_int(file, "map.huts");
      map.generator = secfile_lookup_int(file, "map.generator");
      map.seed = secfile_lookup_int(file, "map.seed");
      map.landpercent = secfile_lookup_int(file, "map.landpercent");
      map.wetness = secfile_lookup_int_default(file, MAP_DEFAULT_WETNESS,
					       "map.wetness");
      map.steepness = secfile_lookup_int_default(file, MAP_DEFAULT_STEEPNESS, 
						 "map.steepness");
      map.have_huts = secfile_lookup_bool_default(file, TRUE,
						  "map.have_huts");
      map.temperature =
	secfile_lookup_int_default(file,
				   MAP_DEFAULT_TEMPERATURE, "map.temperature");
      map.alltemperate
	= secfile_lookup_bool_default(file, MAP_DEFAULT_ALLTEMPERATE,
				      "map.alltemperate");
      map.tinyisles
	= secfile_lookup_bool_default(file, MAP_DEFAULT_TINYISLES,
				      "map.tinyisles");
      map.separatepoles
	= secfile_lookup_bool_default(file, MAP_DEFAULT_SEPARATE_POLES,
				      "map.separatepoles");

      if (has_capability("startoptions", savefile_options)) {
	map.xsize = secfile_lookup_int(file, "map.width");
	map.ysize = secfile_lookup_int(file, "map.height");
      } else {
	/* old versions saved with these names in PRE_GAME_STATE: */
	map.xsize = secfile_lookup_int(file, "map.xsize");
	map.ysize = secfile_lookup_int(file, "map.ysize");
      }

      if (tmp_server_state==PRE_GAME_STATE && map.generator == 0) {
	/* generator 0 = map done with map editor */
	/* aka a "scenario" */
        if (has_capability("specials",savefile_options)) {
          map_load(file, special_order);
          return;
        }
        map_tiles_load(file);
        if (has_capability("riversoverlay",savefile_options)) {
	  map_rivers_overlay_load(file, special_order);
	}
        if (has_capability("startpos",savefile_options)) {
          map_startpos_load(file);
          return;
        }
	return;
      }
    }
    if(tmp_server_state==PRE_GAME_STATE) {
      return;
    }
  }

  /* We check
     1) if the block exists at all.
     2) if it is saved. */
  if (section_file_lookup(file, "random.index_J")
      && secfile_lookup_bool_default(file, TRUE, "game.save_random")) {
    RANDOM_STATE rstate;
    rstate.j = secfile_lookup_int(file,"random.index_J");
    rstate.k = secfile_lookup_int(file,"random.index_K");
    rstate.x = secfile_lookup_int(file,"random.index_X");
    for(i=0;i<8;i++) {
      char name[20];
      my_snprintf(name, sizeof(name), "random.table%d",i);
      string=secfile_lookup_str(file,name);
      sscanf(string,"%8x %8x %8x %8x %8x %8x %8x", &rstate.v[7*i],
	     &rstate.v[7*i+1], &rstate.v[7*i+2], &rstate.v[7*i+3],
	     &rstate.v[7*i+4], &rstate.v[7*i+5], &rstate.v[7*i+6]);
    }
    rstate.is_init = TRUE;
    set_myrand_state(rstate);
  } else {
    /* mark it */
    (void) secfile_lookup_bool_default(file, TRUE, "game.save_random");

    /* We're loading a running game without a seed (which is okay, if it's
     * a scenario).  We need to generate the game seed now because it will
     * be needed later during the load. */
    if (tmp_server_state == RUN_GAME_STATE) {
      init_game_seed();
    }
  }


  game.info.is_new_game = !secfile_lookup_bool_default(file, TRUE,
						  "game.save_players");

  map_load(file, special_order);

  if (!game.info.is_new_game) {
    /* destroyed wonders: */
    string = secfile_lookup_str_default(file, NULL,
                                        "game.destroyed_wonders_new");
    if (!string) {
      /* old savegames */
      string = secfile_lookup_str_default(file, "",
                                          "game.destroyed_wonders");
      for (k = 0; string[k]; k++) {
        if (string[k] == '1') {
	  name = old_impr_type_name(k);
	  id = find_improvement_by_name_orig(name);
	  if (id != -1) {
	    game.info.great_wonders[id] = -1;
	  }
	}
      }
    } else {
      for (k = 0; k < improvement_order_size && string[k]; k++) {
        if (string[k] == '1') {
	  id = find_improvement_by_name_orig(improvement_order[k]);
	  if (id != -1) {
            game.info.great_wonders[id] = -1;
	  }
	}
      }
    }

    players_iterate(pplayer) {
      player_load(pplayer, pplayer->player_no, file, improvement_order,
		  improvement_order_size, technology_order,
		  technology_order_size);
    } players_iterate_end;
    players_iterate(pplayer) {
      /* Some players may have invalid nations in the ruleset.  Pick new
       * nations for them. */
      if (pplayer->nation == NO_NATION_SELECTED) {
	player_set_nation(pplayer, pick_a_nation(NULL, FALSE, TRUE,
                                                 NOT_A_BARBARIAN));
	freelog(LOG_NORMAL, "%s had invalid nation; changing to %s.",
		pplayer->name, pplayer->nation->name);
      }
    } players_iterate_end;

    /* Assign players with no team listed onto an empty team. */
    players_iterate(pplayer) {
      if (!pplayer->team) {
	team_add_player(pplayer, find_empty_team());
      }
    } players_iterate_end;
    
    /* Sanity check alliances, prevent allied-with-ally-of-enemy */
    players_iterate(plr) {
      players_iterate(aplayer) {
        if (plr->is_alive
            && aplayer->is_alive
            && pplayers_allied(plr, aplayer)
            && pplayer_can_make_treaty(plr, aplayer, DS_ALLIANCE) 
               == DIPL_ALLIANCE_PROBLEM) {
          freelog(LOG_ERROR, _("Illegal alliance structure detected: "
                  "%s's alliance to %s reduced to peace treaty."),
                  plr->name, aplayer->name);
          plr->diplstates[aplayer->player_no].type = DS_PEACE;
          aplayer->diplstates[plr->player_no].type = DS_PEACE;
        }
      } players_iterate_end;
    } players_iterate_end;


    /* Update all city information.  This must come after all cities are
     * loaded (in player_load) but before player (dumb) cities are loaded
     * (in player_map_load).
     *
     * This is a bit ugly since generic_city_refresh assumes a city that
     * has already been refreshed at some point (even if it's out of date).
     * Here we follow the simplest method of just refreshing all cities,
     * and updating trade routes at the same time.  This could lead to memory
     * issues because the refresh might look at some data of another city
     * that hasn't itself been refreshed yet.  However this shouldn't cause
     * any problems because in the end all cities are refreshed once (or
     * more) and recursive dependencies are all taken care of. */
    cities_iterate(pcity) {
      generic_city_refresh(pcity, TRUE, NULL);
    } cities_iterate_end;

    /* Since the cities must be placed on the map to put them on the
       player map we do this afterwards */
    for(i = 0; i < game.info.nplayers; i++) {
      player_map_load(&game.players[i], i, file, improvement_order,
		      improvement_order_size, special_order); 
    }

    /* We do this here since if the did it in player_load, player 1
       would try to unfog (unloaded) player 2's map when player 1's units
       were loaded */
    players_iterate(pplayer) {
      pplayer->really_gives_vision = 0;
      pplayer->gives_shared_vision = 0;
    } players_iterate_end;
    players_iterate(pplayer) {
      char *vision;
      int plrno = pplayer->player_no;

      vision = secfile_lookup_str_default(file, NULL,
					  "player%d.gives_shared_vision",
					  plrno);
      if (vision) {
	players_iterate(pplayer2) {
	  if (vision[pplayer2->player_no] == '1') {
	    give_shared_vision(pplayer, pplayer2);
	  }
	} players_iterate_end;
      }
    } players_iterate_end;

    initialize_globals();
    apply_unit_ordering();

    /* Make sure everything is consistent. */
    players_iterate(pplayer) {
      unit_list_iterate(pplayer->units, punit) {
	if (!can_unit_continue_current_activity(punit)) {
	  freelog(LOG_ERROR, "ERROR: Unit doing illegal activity in savegame!");
	  punit->activity = ACTIVITY_IDLE;
	}
      } unit_list_iterate_end;

      city_list_iterate(pplayer->cities, pcity) {
	check_city(pcity);
      } city_list_iterate_end;
    } players_iterate_end;
  } else {
    game.info.nplayers = 0;
  }

  if (secfile_lookup_int_default(file, -1,
				 "game.shuffled_player_%d", 0) >= 0) {
    int shuffled_players[game.info.nplayers];

    for (i = 0; i < game.info.nplayers; i++) {
      shuffled_players[i]
	= secfile_lookup_int(file, "game.shuffled_player_%d", i);
    }
    set_shuffled_players(shuffled_players);
  } else {
    /* No shuffled players included, so shuffle them (this may include
     * scenarios). */
    shuffle_players();
  }

  game.info.player_idx = 0;
  game.player_ptr=&game.players[0];  

  /* Fix ferrying sanity */
  players_iterate(pplayer) {
    unit_list_iterate_safe(pplayer->units, punit) {
      struct unit *ferry = find_unit_by_id(punit->transported_by);

      if (!ferry && !can_unit_exist_at_tile(punit, punit->tile)) {
        freelog(LOG_ERROR, _("Removing %s's unferried %s in %s at (%d, %d)"),
                pplayer->name, unit_name(punit->type),
                get_name(punit->tile->terrain),
                TILE_XY(punit->tile));
        bounce_unit(punit, TRUE);
      }
    } unit_list_iterate_safe_end;
  } players_iterate_end;

  /* Fix stacking issues.  We don't rely on the savegame preserving
   * alliance invariants (old savegames often did not) so if there are any
   * unallied units on the same tile we just bounce them. */
  players_iterate(pplayer) {
    players_iterate(aplayer) {
      resolve_unit_stacks(pplayer, aplayer, TRUE);
    } players_iterate_end;
  } players_iterate_end;

  players_iterate(pplayer) {
    calc_civ_score(pplayer);
  } players_iterate_end;
}

/***************************************************************
...
***************************************************************/
void game_save(struct section_file *file, const char *save_reason)
{
  int i;
  int version;
  char options[512];
  char temp[B_LAST+1];

  version = MAJOR_VERSION *10000 + MINOR_VERSION *100 + PATCH_VERSION; 
  secfile_insert_int(file, version, "game.version");

  /* Game state: once the game is no longer a new game (ie, has been
   * started the first time), it should always be considered a running
   * game for savegame purposes:
   */
  secfile_insert_int(file, (int) (game.info.is_new_game ? server_state :
				  RUN_GAME_STATE), "game.server_state");
  
  secfile_insert_str(file, get_meta_patches_string(), "game.metapatches");
  secfile_insert_str(file, get_meta_message_string(), "game.metamessage");
  secfile_insert_str(file, meta_addr_port(), "game.metaserver");
  
  sz_strlcpy(options, SAVEFILE_OPTIONS);
  if (game.info.is_new_game) {
    if (map.num_start_positions>0) {
      sz_strlcat(options, " startpos");
    }
    if (map.have_resources) {
      sz_strlcat(options, " specials");
    }
    if (map.have_rivers_overlay && !map.have_resources) {
      sz_strlcat(options, " riversoverlay");
    }
  }
  secfile_insert_str(file, options, "savefile.options");
  secfile_insert_str(file, save_reason, "savefile.reason");
  /* Save improvement order in savegame, so we are not dependent on
   * ruleset order.
   * If the game isn't started improvements aren't loaded
   * so we can not save the order.
   */
  if (game.control.num_impr_types > 0) {
    const char* buf[game.control.num_impr_types];
    impr_type_iterate(id) {
      buf[id] = get_improvement_name_orig(id);
    } impr_type_iterate_end;
    secfile_insert_str_vec(file, buf, game.control.num_impr_types,
                           "savefile.improvement_order");
  }
  
  /* Save technology order in savegame, so we are not dependent on ruleset
   * order. If the game isn't started advances aren't loaded 
   * so we can not save the order. */
  if (game.control.num_tech_types > 0) {
    const char* buf[game.control.num_tech_types];
    tech_type_iterate(tech) {
      if (tech == A_NONE) {
        buf[tech] = "A_NONE";
      } else {
        buf[tech] = advances[tech].name_orig;
      }
    } tech_type_iterate_end;
    secfile_insert_str_vec(file, buf, game.control.num_tech_types,
                           "savefile.technology_order");
  }
  {
    const char **modname;
    enum tile_special_type j;

    /* Save specials order */
    modname = fc_calloc(S_LAST, sizeof(*modname));
    for (j = 0; j < S_LAST; j++) {
      modname[j] = get_special_name_orig(j);
    }
    secfile_insert_str_vec(file, modname, S_LAST,
			   "savefile.specials");
    free(modname);
  }

  
  secfile_insert_int(file, game.info.gold, "game.gold");
  secfile_insert_int(file, game.info.tech, "game.tech");
  secfile_insert_int(file, game.info.skill_level, "game.skill_level");
  secfile_insert_int(file, game.info.timeout, "game.timeout");
  secfile_insert_int(file, game.timeoutint, "game.timeoutint");
  secfile_insert_int(file, game.timeoutintinc, "game.timeoutintinc");
  secfile_insert_int(file, game.timeoutinc, "game.timeoutinc");
  secfile_insert_int(file, game.timeoutincmult, "game.timeoutincmult"); 
  secfile_insert_int(file, game.timeoutcounter, "game.timeoutcounter");
  secfile_insert_int(file, game.timeoutaddenemymove,
		     "game.info.timeoutaddenemymove");
  secfile_insert_int(file, game.info.end_year, "game.end_year");
  secfile_insert_int(file, game.info.year, "game.year");
  secfile_insert_int(file, game.info.turn, "game.turn");
  secfile_insert_bool(file, game.info.simultaneous_phases,
		      "game.simultaneous_phases_now");
  secfile_insert_bool(file, game.simultaneous_phases_stored,
		      "game.simultaneous_phases_stored");
  secfile_insert_int(file, game.info.min_players, "game.min_players");
  secfile_insert_int(file, game.info.max_players, "game.max_players");
  secfile_insert_int(file, game.info.nplayers, "game.nplayers");
  secfile_insert_int(file, game.info.heating, "game.heating");
  secfile_insert_int(file, game.info.globalwarming, "game.globalwarming");
  secfile_insert_int(file, game.info.warminglevel, "game.warminglevel");
  secfile_insert_int(file, game.info.nuclearwinter, "game.nuclearwinter");
  secfile_insert_int(file, game.info.cooling, "game.cooling");
  secfile_insert_int(file, game.info.coolinglevel, "game.coolinglevel");
  secfile_insert_int(file, game.info.notradesize, "game.notradesize");
  secfile_insert_int(file, game.info.fulltradesize, "game.fulltradesize");
  secfile_insert_bool(file, game.info.angrycitizen, "game.angrycitizen");
  secfile_insert_int(file, game.info.citymindist, "game.citymindist");
  secfile_insert_int(file, game.info.civilwarsize, "game.civilwarsize");
  secfile_insert_int(file, game.info.contactturns, "game.contactturns");
  secfile_insert_int(file, game.info.rapturedelay, "game.rapturedelay");
  secfile_insert_int(file, game.info.diplcost, "game.diplcost");
  secfile_insert_int(file, game.info.freecost, "game.freecost");
  secfile_insert_int(file, game.info.conquercost, "game.conquercost");
  secfile_insert_int(file, game.info.foodbox, "game.box_food");
  secfile_insert_int(file, game.info.shieldbox, "game.box_shield");
  secfile_insert_int(file, game.info.sciencebox, "game.box_science");
  {
    /* These values are for compatibility with 2.0 and previous servers. */
    secfile_insert_int(file, game.info.sciencebox / 5, "game.researchcost");
  secfile_insert_int(file, game.info.foodbox / 10, "game.foodbox");
  }
  secfile_insert_int(file, game.info.techpenalty, "game.techpenalty");
  secfile_insert_int(file, game.info.razechance, "game.razechance");

  /* Write civstyle for compatibility with old servers */
  secfile_insert_int(file, 2, "game.civstyle");
  secfile_insert_int(file, game.info.save_nturns, "game.save_nturns");
  secfile_insert_str(file, game.save_name, "game.save_name");
  secfile_insert_int(file, game.info.aifill, "game.aifill");
  secfile_insert_bool(file, game.scorelog, "game.scorelog");
  secfile_insert_str(file, game.id, "game.id");
  secfile_insert_bool(file, game.info.fogofwar, "game.fogofwar");
  secfile_insert_bool(file, game.info.spacerace, "game.spacerace");
  secfile_insert_bool(file, game.info.auto_ai_toggle, "game.auto_ai_toggle");
  secfile_insert_int(file, game.info.diplchance, "game.diplchance");
  secfile_insert_int(file, game.info.aqueductloss, "game.aqueductloss");
  secfile_insert_int(file, game.info.killcitizen, "game.killcitizen");
  secfile_insert_bool(file, game.info.turnblock, "game.turnblock");
  secfile_insert_bool(file, game.info.savepalace, "game.savepalace");
  secfile_insert_bool(file, game.info.fixedlength, "game.fixedlength");
  secfile_insert_int(file, game.info.barbarianrate, "game.barbarians");
  secfile_insert_int(file, game.info.onsetbarbarian, "game.onsetbarbs");
  secfile_insert_int(file, game.info.revolution_length, "game.revolen");
  secfile_insert_int(file, game.info.occupychance, "game.occupychance");
  secfile_insert_bool(file, game.info.autoattack, "game.autoattack");
  secfile_insert_str(file, game.demography, "game.demography");
  secfile_insert_str(file, game.allow_take, "game.allow_take");
  secfile_insert_int(file, game.info.borders, "game.borders");
  secfile_insert_bool(file, game.info.happyborders, "game.happyborders");
  secfile_insert_int(file, game.info.diplomacy, "game.diplomacy");
  secfile_insert_int(file, game.info.allowed_city_names, "game.allowed_city_names");

  /* old (1.14.1) servers need to have these server variables.  The values
   * don't matter, though. */
  secfile_insert_int(file, 2, "game.settlers");
  secfile_insert_int(file, 1, "game.explorer");
  secfile_insert_int(file, 30, "map.mountains");
  secfile_insert_int(file, 35, "map.grass");
  secfile_insert_int(file, 5, "map.swampsize");
  secfile_insert_int(file, 5, "map.deserts");
  secfile_insert_int(file, 5, "map.riverlength");
  secfile_insert_int(file, 20, "map.forestsize");

  if (TRUE) {
    /* Now always save these, so the server options reflect the
     * actual values used at the start of the game.
     * The first two used to be saved as "map.xsize" and "map.ysize"
     * when PRE_GAME_STATE, but I'm standardizing on width,height --dwp
     */
    secfile_insert_int(file, map.topology_id, "map.topology_id");
    secfile_insert_int(file, map.size, "map.size");
    secfile_insert_int(file, map.xsize, "map.width");
    secfile_insert_int(file, map.ysize, "map.height");
    secfile_insert_str(file, game.info.start_units, "game.start_units");
    secfile_insert_int(file, game.info.dispersion, "game.dispersion");
    secfile_insert_int(file, map.seed, "map.seed");
    secfile_insert_int(file, map.landpercent, "map.landpercent");
    secfile_insert_int(file, map.riches, "map.riches");
    secfile_insert_int(file, map.wetness, "map.wetness");
    secfile_insert_int(file, map.steepness, "map.steepness");
    secfile_insert_int(file, map.huts, "map.huts");
    secfile_insert_int(file, map.generator, "map.generator");
    secfile_insert_bool(file, map.have_huts, "map.have_huts");
    secfile_insert_int(file, map.temperature, "map.temperature");
    secfile_insert_bool(file, map.alltemperate, "map.alltemperate");
    secfile_insert_bool(file, map.tinyisles, "map.tinyisles");
    secfile_insert_bool(file, map.separatepoles, "map.separatepoles");
  } 

  secfile_insert_int(file, game.seed, "game.randseed");
  
  if (myrand_is_init() && game.save_options.save_random) {
    RANDOM_STATE rstate = get_myrand_state();
    secfile_insert_int(file, 1, "game.save_random");
    assert(rstate.is_init);

    secfile_insert_int(file, rstate.j, "random.index_J");
    secfile_insert_int(file, rstate.k, "random.index_K");
    secfile_insert_int(file, rstate.x, "random.index_X");

    for (i = 0; i < 8; i++) {
      char name[20], vec[100];

      my_snprintf(name, sizeof(name), "random.table%d", i);
      my_snprintf(vec, sizeof(vec),
		  "%8x %8x %8x %8x %8x %8x %8x", rstate.v[7 * i],
		  rstate.v[7 * i + 1], rstate.v[7 * i + 2],
		  rstate.v[7 * i + 3], rstate.v[7 * i + 4],
		  rstate.v[7 * i + 5], rstate.v[7 * i + 6]);
      secfile_insert_str(file, vec, name);
    }
  } else {
    secfile_insert_int(file, 0, "game.save_random");
  }

  secfile_insert_str(file, game.rulesetdir, "game.rulesetdir");

  if (!map_is_empty()) {
    map_save(file);
  }
  
  script_state_save(file);

  if ((server_state == PRE_GAME_STATE) && game.info.is_new_game) {
    return; /* want to save scenarios as well */
  }

  secfile_insert_bool(file, game.save_options.save_players,
		      "game.save_players");
  if (game.save_options.save_players) {
    /* 1.14 servers depend on improvement order in ruleset. Here we
     * are trying to simulate 1.14.1 default order
     */
    init_old_improvement_bitvector(temp);
    impr_type_iterate(id) {
      if (is_great_wonder(id) && great_wonder_was_built(id)
	  && !find_city_from_great_wonder(id)) {
        add_improvement_into_old_bitvector(temp, id);
      } 
    } impr_type_iterate_end;
    secfile_insert_str(file, temp, "game.destroyed_wonders");
    
    /* Save destroyed wonders as bitvector. Note that improvement order
     * is saved in savefile.improvement_order
     */
    impr_type_iterate(id) {
      if (is_great_wonder(id) && great_wonder_was_built(id)
	  && !find_city_from_great_wonder(id)) {
	temp[id] = '1';
      } else {
        temp[id] = '0';
      }
    } impr_type_iterate_end;
    temp[game.control.num_impr_types] = '\0';
    secfile_insert_str(file, temp, "game.destroyed_wonders_new");

    calc_unit_ordering();

    players_iterate(pplayer) {
      player_save(pplayer, pplayer->player_no, file);
    } players_iterate_end;

    for (i = 0; i < game.info.nplayers; i++) {
      secfile_insert_int(file, shuffled_player(i)->player_no,
			 "game.shuffled_player_%d", i);
    }
  }
}
