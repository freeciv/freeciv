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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capability.h"
#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "idex.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "rand.h"
#include "registry.h"
#include "shared.h"
#include "support.h"
#include "unit.h"
#include "version.h"

#include "citytools.h"
#include "cityturn.h"
#include "mapgen.h"
#include "maphand.h"
#include "meta.h"
#include "plrhand.h"
#include "ruleset.h"
#include "spacerace.h"
#include "srv_main.h"
#include "unittools.h"

#include "aicity.h"
#include "aiunit.h"

#include "savegame.h"

/* 
 * This loops over the entire map to save data. It collects all the
 * data of a line using get_xy_char and then executes the
 * secfile_insert_line code. The macro provides the variables x and y
 * and line. Parameter:
 * - get_xy_char: code that returns the character for each (x, y)
 * coordinate.
 *  - secfile_insert_line: code which is executed every time a line is
 *  processed
 */

#define SAVE_MAP_DATA(get_xy_char, secfile_insert_line) \
{                                                       \
  char *line = fc_malloc(map.xsize + 1);                \
  int x, y;                                             \
                                                        \
  for (y = 0; y < map.ysize; y++) {                     \
    for (x = 0; x < map.xsize; x++) {                   \
      if (regular_map_pos_is_normal(x, y)) {            \
	line[x] = get_xy_char;                          \
        if(!my_isprint(line[x] & 0x7f)) {               \
          freelog(LOG_FATAL, _("Trying to write invalid"\
		  " map data: '%c' %d"),                \
		  line[x], line[x]);                    \
          exit(EXIT_FAILURE);                           \
        }                                               \
      } else {                                          \
        /* skipped over in loading */                   \
	line[x] = '#';                                  \
      }                                                 \
    }                                                   \
    line[map.xsize] = '\0';                             \
    secfile_insert_line;                                \
  }                                                     \
  free(line);                                           \
}

#define SAVE_NORMAL_MAP_DATA(secfile, get_xy_char, secfile_name)          \
	SAVE_MAP_DATA(get_xy_char,                                        \
		      secfile_insert_str(secfile, line, secfile_name, y))

#define SAVE_PLAYER_MAP_DATA(secfile, get_xy_char, secfile_name, plrno)   \
	SAVE_MAP_DATA(get_xy_char,                                        \
		      secfile_insert_str(secfile, line, secfile_name,     \
					 plrno, y))

/* This loops over the entire map to load data. It loads all the data
 * of a line using secfile_lookup_line and then executes the
 * set_xy_char code for every position in that line. The macro
 * provides the variables x and y and ch (ch is the current character
 * of the line). Parameters:
 * - set_xy_char: code that sets the character for each (x, y)
 * coordinate.
 *  - secfile_lookup_line: code which is executed every time a line is
 *  processed; it returns a char* for the line */

/* Note: some (but not all) of the code this is replacing used to
 * skip over lines that did not exist.  This allowed for
 * backward-compatibility.  We could add another parameter that
 * specified whether it was OK to skip the data, but there's not
 * really much advantage to exiting early in this case.  Instead,
 * we let any map data type to be empty, and just print an
 * informative warning message about it. */

#define LOAD_MAP_DATA(secfile_lookup_line, set_xy_char)                       \
{                                                                             \
  int y;                                                                      \
  bool warning_printed = FALSE;                                               \
  for (y = 0; y < map.ysize; y++) {                                           \
    char *line = secfile_lookup_line;                                         \
    int x;                                                                    \
    if (!line || strlen(line) != map.xsize) {                                 \
      if(!warning_printed) {                                                  \
        freelog(LOG_ERROR, _("The save file contains incomplete "             \
                "map data.  This can happen with old saved "                  \
                "games, or it may indicate an invalid saved "                 \
                "game file.  Proceed at your own risk."));                    \
        if(!line) {                                                           \
          freelog(LOG_ERROR, _("Reason: line not found"));                    \
        } else {                                                              \
          freelog(LOG_ERROR, _("Reason: line too short "                      \
                  "(expected %d got %lu"), map.xsize,                         \
                  (unsigned long) strlen(line));                              \
        }                                                                     \
        freelog(LOG_ERROR, "secfile_lookup_line='%s'",                        \
                #secfile_lookup_line);                                        \
        warning_printed = TRUE;                                               \
      }                                                                       \
      continue;                                                               \
    }                                                                         \
    for(x = 0; x < map.xsize; x++) {                                          \
      char ch = line[x];                                                      \
      if (regular_map_pos_is_normal(x, y)) {                                  \
        set_xy_char;                                                          \
      } else {                                                                \
        assert(ch == '#');                                                    \
      }                                                                       \
    }                                                                         \
  }                                                                           \
}

/* The following should be removed when compatibility with
   pre-1.13.0 savegames is broken: startoptions, spacerace2
   and rulesets */
#define SAVEFILE_OPTIONS "startoptions spacerace2 rulesets" \
" diplchance_percent worklists2 map_editor known32fix turn " \
"attributes watchtower rulesetdir client_worklists"

static const char hex_chars[] = "0123456789abcdef";
static const char terrain_chars[] = "adfghjm prstu";

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
    freelog(LOG_FATAL, "Unknown hex value: '%c' %d", ch, ch);
    exit(EXIT_FAILURE);
  }
  return (pch - hex_chars) << (halfbyte * 4);
}

/***************************************************************
Dereferences the terrain character.  See terrain_chars[].
  example: char2terrain('a') == 0
***************************************************************/
static int char2terrain(char ch)
{
  char *pch = strchr(terrain_chars, ch);

  if (!pch || ch == '\0') {
    freelog(LOG_FATAL, "Unknown terrain type: '%c' %d", ch, ch);
    exit(EXIT_FAILURE);
  }
  return pch - terrain_chars;
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
  int i=0, pos;

  map.fixed_start_positions = secfile_lookup_bool_default(file, FALSE, "map.fixed_start_positions");

  while( (pos = secfile_lookup_int_default(file, -1, "map.r%dsx", i)) != -1) {
    map.start_positions[i].x = pos;
    map.start_positions[i].y = secfile_lookup_int(file, "map.r%dsy", i);
    i++;
  }

  if (i < game.max_players) {
    freelog(LOG_VERBOSE,
	    _("Number of starts (%d) are lower than max_players (%d),"
	      " lowering max_players."),
 	    i, game.max_players);
    game.max_players = i;
  }

  map.num_start_positions = i;
}

/***************************************************************
load the tile map from a savegame file
***************************************************************/
static void map_tiles_load(struct section_file *file)
{
  map.is_earth=secfile_lookup_bool(file, "map.is_earth");

  /* In some cases we read these before, but not always, and
   * its safe to read them again:
   */
  map.xsize=secfile_lookup_int(file, "map.width");
  map.ysize=secfile_lookup_int(file, "map.height");

  map_allocate();

  /* get the terrain type */
  LOAD_MAP_DATA(secfile_lookup_str(file, "map.t%03d", y),
		map_get_tile(x, y)->terrain = char2terrain(ch));

  assign_continent_numbers();
}

/***************************************************************
load the rivers overlay map from a savegame file

(This does not need to be called from map_load(), because
 map_load() loads the rivers overlay along with the rest of
 the specials.  Call this only if you've already called
 map_tiles_load(), and want to overlay rivers defined as
 specials, rather than as terrain types.)
***************************************************************/
static void map_rivers_overlay_load(struct section_file *file)
{
  /* Get the bits of the special flags which contain the river special
     and extract the rivers overlay from them. */
  LOAD_MAP_DATA(secfile_lookup_str_default(file, NULL, "map.n%03d", y),
		map_get_tile(x, y)->special |=
		(ascii_hex2bin(ch, 2) & S_RIVER));
  map.have_rivers_overlay = TRUE;
}

/***************************************************************
load a complete map from a savegame file
***************************************************************/
static void map_load(struct section_file *file)
{
  char *savefile_options = secfile_lookup_str(file, "savefile.options");

  /* map_init();
   * This is already called in game_init(), and calling it
   * here stomps on map.huts etc.  --dwp
   */

  map_tiles_load(file);
  if (secfile_lookup_bool_default(file, TRUE, "game.save_starts")
      && game.load_options.load_starts) {
    map_startpos_load(file);
  } else {
    map.num_start_positions = 0;
    map.fixed_start_positions = FALSE;
  }

  /* get 4-bit segments of 16-bit "special" field. */
  LOAD_MAP_DATA(secfile_lookup_str(file, "map.l%03d", y),
		map_get_tile(x, y)->special = ascii_hex2bin(ch, 0));
  LOAD_MAP_DATA(secfile_lookup_str(file, "map.u%03d", y),
		map_get_tile(x, y)->special |= ascii_hex2bin(ch, 1));
  LOAD_MAP_DATA(secfile_lookup_str_default(file, NULL, "map.n%03d", y),
		map_get_tile(x, y)->special |= ascii_hex2bin(ch, 2));
  LOAD_MAP_DATA(secfile_lookup_str_default(file, NULL, "map.f%03d", y),
		map_get_tile(x, y)->special |= ascii_hex2bin(ch, 3));

  if (secfile_lookup_bool_default(file, TRUE, "game.save_known")
      && game.load_options.load_known) {

    /* get 4-bit segments of the first half of the 32-bit "known" field */
    LOAD_MAP_DATA(secfile_lookup_str(file, "map.a%03d", y),
		  map_get_tile(x, y)->known = ascii_hex2bin(ch, 0));
    LOAD_MAP_DATA(secfile_lookup_str(file, "map.b%03d", y),
		  map_get_tile(x, y)->known |= ascii_hex2bin(ch, 1));
    LOAD_MAP_DATA(secfile_lookup_str(file, "map.c%03d", y),
		  map_get_tile(x, y)->known |= ascii_hex2bin(ch, 2));
    LOAD_MAP_DATA(secfile_lookup_str(file, "map.d%03d", y),
		  map_get_tile(x, y)->known |= ascii_hex2bin(ch, 3));

    if (has_capability("known32fix", savefile_options)) {
      /* get 4-bit segments of the second half of the 32-bit "known" field */
      LOAD_MAP_DATA(secfile_lookup_str(file, "map.e%03d", y),
		    map_get_tile(x, y)->known |= ascii_hex2bin(ch, 4));
      LOAD_MAP_DATA(secfile_lookup_str(file, "map.g%03d", y),
		    map_get_tile(x, y)->known |= ascii_hex2bin(ch, 5));
      LOAD_MAP_DATA(secfile_lookup_str(file, "map.h%03d", y),
		    map_get_tile(x, y)->known |= ascii_hex2bin(ch, 6));
      LOAD_MAP_DATA(secfile_lookup_str(file, "map.i%03d", y),
		    map_get_tile(x, y)->known |= ascii_hex2bin(ch, 7));
    }
  }


  map.have_specials = TRUE;

  /* Should be handled as part of send_all_know_tiles,
     but do it here too for safety */
  whole_map_iterate(x, y) {
    map_get_tile(x, y)->sent = 0;
  } whole_map_iterate_end;
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
  secfile_insert_bool(file, map.is_earth, "map.is_earth");

  secfile_insert_bool(file, game.save_options.save_starts, "game.save_starts");
  if (game.save_options.save_starts) {
    secfile_insert_bool(file, map.fixed_start_positions, "map.fixed_start_positions");
    for (i=0; i<map.num_start_positions; i++) {
      secfile_insert_int(file, map.start_positions[i].x, "map.r%dsx", i);
      secfile_insert_int(file, map.start_positions[i].y, "map.r%dsy", i);
    }
  }
    
  /* put the terrain type */
  SAVE_NORMAL_MAP_DATA(file, terrain_chars[map_get_tile(x, y)->terrain],
		       "map.t%03d");

  if (!map.have_specials) {
    if (map.have_rivers_overlay) {
      /* 
       * Save the rivers overlay map; this is a special case to allow
       * re-saving scenarios which have rivers overlay data.  This only
       * applies if don't have rest of specials.
       */

      /* bits 8-11 of special flags field */
      SAVE_NORMAL_MAP_DATA(file,
			   bin2ascii_hex(map_get_tile(x, y)->special, 2),
			   "map.n%03d");
    }
    return;
  }

  /* put 4-bit segments of 12-bit "special flags" field */
  SAVE_NORMAL_MAP_DATA(file, bin2ascii_hex(map_get_tile(x, y)->special, 0),
		       "map.l%03d");
  SAVE_NORMAL_MAP_DATA(file, bin2ascii_hex(map_get_tile(x, y)->special, 1),
		       "map.u%03d");
  SAVE_NORMAL_MAP_DATA(file, bin2ascii_hex(map_get_tile(x, y)->special, 2),
		       "map.n%03d");

  secfile_insert_bool(file, game.save_options.save_known, "game.save_known");
  if (game.save_options.save_known) {
    /* put the top 4 bits (bits 12-15) of special flags */
    SAVE_NORMAL_MAP_DATA(file,
			 bin2ascii_hex(map_get_tile(x, y)->special, 3),
			 "map.f%03d");

    /* put 4-bit segments of the 32-bit "known" field */
    SAVE_NORMAL_MAP_DATA(file, bin2ascii_hex(map_get_tile(x, y)->known, 0),
			 "map.a%03d");
    SAVE_NORMAL_MAP_DATA(file, bin2ascii_hex(map_get_tile(x, y)->known, 1),
			 "map.b%03d");
    SAVE_NORMAL_MAP_DATA(file, bin2ascii_hex(map_get_tile(x, y)->known, 2),
			 "map.c%03d");
    SAVE_NORMAL_MAP_DATA(file, bin2ascii_hex(map_get_tile(x, y)->known, 3),
			 "map.d%03d");
    SAVE_NORMAL_MAP_DATA(file, bin2ascii_hex(map_get_tile(x, y)->known, 4),
			 "map.e%03d");
    SAVE_NORMAL_MAP_DATA(file, bin2ascii_hex(map_get_tile(x, y)->known, 5),
			 "map.g%03d");
    SAVE_NORMAL_MAP_DATA(file, bin2ascii_hex(map_get_tile(x, y)->known, 6),
			 "map.h%03d");
    SAVE_NORMAL_MAP_DATA(file, bin2ascii_hex(map_get_tile(x, y)->known, 7),
			 "map.i%03d");
  }
}

/***************************************************************
Load the worklist elements specified by path, given the arguments
plrno and wlinx, into the worklist pointed to by pwl.
***************************************************************/
static void worklist_load(struct section_file *file,
			  char *path, int plrno, int wlinx,
			  struct worklist *pwl)
{
  char efpath[64];
  char idpath[64];
  int i;
  bool end = FALSE;

  sz_strlcpy(efpath, path);
  sz_strlcat(efpath, ".wlef%d");
  sz_strlcpy(idpath, path);
  sz_strlcat(idpath, ".wlid%d");

  for (i = 0; i < MAX_LEN_WORKLIST; i++) {
    if (end) {
      pwl->wlefs[i] = WEF_END;
      pwl->wlids[i] = 0;
      (void) section_file_lookup(file, efpath, plrno, wlinx, i);
      (void) section_file_lookup(file, idpath, plrno, wlinx, i);
    } else {
      pwl->wlefs[i] =
	secfile_lookup_int_default(file, WEF_END, efpath, plrno, wlinx, i);
      pwl->wlids[i] =
	secfile_lookup_int_default(file, 0, idpath, plrno, wlinx, i);

      if ((pwl->wlefs[i] <= WEF_END) || (pwl->wlefs[i] >= WEF_LAST) ||
	  ((pwl->wlefs[i] == WEF_UNIT) && !unit_type_exists(pwl->wlids[i])) ||
	  ((pwl->wlefs[i] == WEF_IMPR) && !improvement_exists(pwl->wlids[i]))) {
	pwl->wlefs[i] = WEF_END;
	pwl->wlids[i] = 0;
	end = TRUE;
      }
    }
  }
}

/***************************************************************
Load the worklist elements specified by path, given the arguments
plrno and wlinx, into the worklist pointed to by pwl.
Assumes original save-file format.  Use for backward compatibility.
***************************************************************/
static void worklist_load_old(struct section_file *file,
			      char *path, int plrno, int wlinx,
			      struct worklist *pwl)
{
  int i, id;
  bool end = FALSE;

  for (i = 0; i < MAX_LEN_WORKLIST; i++) {
    if (end) {
      pwl->wlefs[i] = WEF_END;
      pwl->wlids[i] = 0;
      (void) section_file_lookup(file, path, plrno, wlinx, i);
    } else {
      id = secfile_lookup_int_default(file, -1, path, plrno, wlinx, i);

      if ((id < 0) || (id >= 284)) {	/* 284 was flag value for end of list */
	pwl->wlefs[i] = WEF_END;
	pwl->wlids[i] = 0;
	end = TRUE;
      } else if (id >= 68) {		/* 68 was offset to unit ids */
	pwl->wlefs[i] = WEF_UNIT;
	pwl->wlids[i] = id - 68;
	end = !unit_type_exists(pwl->wlids[i]);
      } else {				/* must be an improvement id */
	pwl->wlefs[i] = WEF_IMPR;
	pwl->wlids[i] = id;
	end = !improvement_exists(pwl->wlids[i]);
      }
    }
  }
}

/***************************************************************
...
***************************************************************/
static void player_load(struct player *plr, int plrno,
			struct section_file *file)
{
  int i, j, x, y, nunits, ncities;
  char *p;
  char *savefile_options = secfile_lookup_str(file, "savefile.options");

  server_player_init(plr, TRUE);

  plr->ai.barbarian_type = secfile_lookup_int_default(file, 0, "player%d.ai.is_barbarian",
                                                    plrno);
  if (is_barbarian(plr)) game.nbarbarians++;

  sz_strlcpy(plr->name, secfile_lookup_str(file, "player%d.name", plrno));
  sz_strlcpy(plr->username,
	     secfile_lookup_str_default(file, "", "player%d.username", plrno));
  plr->nation=secfile_lookup_int(file, "player%d.race", plrno);
  if (is_barbarian(plr)) {
    plr->nation=game.nation_count-1;
  }
  plr->government=secfile_lookup_int(file, "player%d.government", plrno);
  plr->embassy=secfile_lookup_int(file, "player%d.embassy", plrno);
  plr->city_style=secfile_lookup_int_default(file, get_nation_city_style(plr->nation),
                                             "player%d.city_style", plrno);

  plr->nturns_idle=0;
  plr->is_male=secfile_lookup_bool_default(file, TRUE, "player%d.is_male", plrno);
  plr->is_alive=secfile_lookup_bool(file, "player%d.is_alive", plrno);
  plr->ai.control = secfile_lookup_bool(file, "player%d.ai.control", plrno);
  plr->ai.tech_goal = secfile_lookup_int(file, "player%d.ai.tech_goal", plrno);
  plr->ai.handicap = 0;		/* set later */
  plr->ai.fuzzy = 0;		/* set later */
  plr->ai.expand = 100;		/* set later */
  plr->ai.skill_level =
    secfile_lookup_int_default(file, game.skill_level,
			       "player%d.ai.skill_level", plrno);
  if (plr->ai.control && plr->ai.skill_level==0) {
    plr->ai.skill_level = GAME_OLD_DEFAULT_SKILL_LEVEL;
  }

  plr->economic.gold=secfile_lookup_int(file, "player%d.gold", plrno);
  plr->economic.tax=secfile_lookup_int(file, "player%d.tax", plrno);
  plr->economic.science=secfile_lookup_int(file, "player%d.science", plrno);
  plr->economic.luxury=secfile_lookup_int(file, "player%d.luxury", plrno);

  plr->future_tech=secfile_lookup_int(file, "player%d.futuretech", plrno);

  plr->research.bulbs_researched=secfile_lookup_int(file, 
					     "player%d.researched", plrno);
  plr->research.techs_researched=secfile_lookup_int(file, 
					     "player%d.researchpoints", plrno);
  plr->research.researching=secfile_lookup_int(file, 
					     "player%d.researching", plrno);

  p=secfile_lookup_str(file, "player%d.invs", plrno);
    
  plr->capital=secfile_lookup_bool(file, "player%d.capital", plrno);
  plr->revolution=secfile_lookup_int_default(file, 0, "player%d.revolution",
                                             plrno);

  for(i=0; i<game.num_tech_types; i++)
    set_invention(plr, i, (p[i]=='1') ? TECH_KNOWN : TECH_UNKNOWN);

  update_research(plr);

  plr->reputation=secfile_lookup_int_default(file, GAME_DEFAULT_REPUTATION,
					     "player%d.reputation", plrno);
  for(i=0; i<game.nplayers; i++) {
    plr->diplstates[i].type = 
      secfile_lookup_int_default(file, DS_WAR,
				 "player%d.diplstate%d.type", plrno, i);
    plr->diplstates[i].turns_left = 
      secfile_lookup_int_default(file, -2,
				 "player%d.diplstate%d.turns_left", plrno, i);
    plr->diplstates[i].has_reason_to_cancel = 
      secfile_lookup_int_default(file, 0,
				 "player%d.diplstate%d.has_reason_to_cancel",
				 plrno, i);
  }
  /* We don't need this info, but savegames carry it anyway.
     To avoid getting "unused" warnings we touch the values like this. */
  for (i=game.nplayers; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
    secfile_lookup_int_default(file, DS_NEUTRAL,
			       "player%d.diplstate%d.type", plrno, i);
    secfile_lookup_int_default(file, 0,
			       "player%d.diplstate%d.turns_left", plrno, i);
    secfile_lookup_int_default(file, 0,
			       "player%d.diplstate%d.has_reason_to_cancel",
			       plrno, i);
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

  city_list_init(&plr->cities);
  ncities=secfile_lookup_int(file, "player%d.ncities", plrno);
  
  for(i=0; i<ncities; i++) { /* read the cities */
    struct city *pcity;
    
    pcity=fc_malloc(sizeof(struct city));
    pcity->ai.ai_role = AICITY_NONE;
    pcity->ai.trade_want = TRADE_WEIGHTING;
    memset(pcity->ai.building_want, 0, sizeof(pcity->ai.building_want));
    pcity->ai.workremain = 1; /* there's always work to be done! */
    pcity->ai.danger = -1; /* flag, may come in handy later */
    pcity->corruption = 0;
    pcity->shield_bonus = 100;
    pcity->tax_bonus = 100;
    pcity->science_bonus = 100;
 
    pcity->id=secfile_lookup_int(file, "player%d.c%d.id", plrno, i);
    alloc_id(pcity->id);
    idex_register_city(pcity);
    pcity->owner=plrno;
    pcity->x=secfile_lookup_int(file, "player%d.c%d.x", plrno, i);
    pcity->y=secfile_lookup_int(file, "player%d.c%d.y", plrno, i);
    
    sz_strlcpy(pcity->name,
	       secfile_lookup_str(file, "player%d.c%d.name", plrno, i));
    if (section_file_lookup(file, "player%d.c%d.original", plrno, i))
      pcity->original = secfile_lookup_int(file, "player%d.c%d.original", 
					   plrno,i);
    else 
      pcity->original = plrno;
    pcity->size=secfile_lookup_int(file, "player%d.c%d.size", plrno, i);

    pcity->steal=secfile_lookup_int(file, "player%d.c%d.steal", plrno, i);

    pcity->ppl_elvis=secfile_lookup_int(file, "player%d.c%d.nelvis", plrno, i);
    pcity->ppl_scientist=secfile_lookup_int(file, 
					  "player%d.c%d.nscientist", plrno, i);
    pcity->ppl_taxman=secfile_lookup_int(file, "player%d.c%d.ntaxman",
					 plrno, i);

    for (j = 0; j < NUM_TRADEROUTES; j++)
      pcity->trade[j]=secfile_lookup_int(file, "player%d.c%d.traderoute%d",
					 plrno, i, j);
    
    pcity->food_stock=secfile_lookup_int(file, "player%d.c%d.food_stock", 
						 plrno, i);
    pcity->shield_stock=secfile_lookup_int(file, "player%d.c%d.shield_stock", 
						   plrno, i);
    pcity->tile_trade=pcity->trade_prod=0;
    pcity->anarchy=secfile_lookup_int(file, "player%d.c%d.anarchy", plrno,i);
    pcity->rapture=secfile_lookup_int_default(file, 0, "player%d.c%d.rapture", plrno,i);
    pcity->was_happy=secfile_lookup_bool(file, "player%d.c%d.was_happy", plrno,i);
    pcity->is_building_unit=
      secfile_lookup_bool(file, 
			 "player%d.c%d.is_building_unit", plrno, i);
    pcity->currently_building=
      secfile_lookup_int(file, 
			 "player%d.c%d.currently_building", plrno, i);
    pcity->turn_last_built=
      secfile_lookup_int_default(file, GAME_START_YEAR,
				 "player%d.c%d.turn_last_built", plrno, i);
    pcity->turn_changed_target=
      secfile_lookup_int_default(file, GAME_START_YEAR,
				 "player%d.c%d.turn_changed_target", plrno, i);
    pcity->changed_from_id=
      secfile_lookup_int_default(file, pcity->currently_building,
				 "player%d.c%d.changed_from_id", plrno, i);
    pcity->changed_from_is_unit=
      secfile_lookup_bool_default(file, pcity->is_building_unit,
				 "player%d.c%d.changed_from_is_unit", plrno, i);
    pcity->before_change_shields=
      secfile_lookup_int_default(file, pcity->shield_stock,
				 "player%d.c%d.before_change_shields", plrno, i);
    pcity->disbanded_shields=
      secfile_lookup_int_default(file, 0,
				 "player%d.c%d.disbanded_shields", plrno, i);
    pcity->caravan_shields=
      secfile_lookup_int_default(file, 0,
				 "player%d.c%d.caravan_shields", plrno, i);

    pcity->did_buy=secfile_lookup_int(file,
				      "player%d.c%d.did_buy", plrno,i);
    pcity->did_sell =
      secfile_lookup_bool_default(file, FALSE, "player%d.c%d.did_sell", plrno,i);
    
    if (game.version >=10300) 
      pcity->airlift=secfile_lookup_bool(file,
					"player%d.c%d.airlift", plrno,i);
    else
      pcity->airlift = FALSE;

    pcity->city_options =
      secfile_lookup_int_default(file, CITYOPT_DEFAULT,
				 "player%d.c%d.options", plrno, i);

    /* Fix for old buggy savegames. */
    if (!has_capability("known32fix", savefile_options)
	&& plrno >= 16) {
      map_city_radius_iterate(pcity->x, pcity->y, x1, y1) {
	map_set_known(x1, y1, plr);
      } map_city_radius_iterate_end;
    }
    
    /* adding the cities contribution to fog-of-war */
    map_unfog_pseudo_city_area(&game.players[plrno],pcity->x,pcity->y);

    unit_list_init(&pcity->units_supported);

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
	  int map_x, map_y;

	  set_worker_city(pcity, x, y,
			  city_map_to_map(&map_x, &map_y, pcity, x, y) ?
			  C_TILE_EMPTY : C_TILE_UNAVAILABLE);
	} else if (*p=='1') {
	  int map_x, map_y;
	  bool is_real;

	  is_real = city_map_to_map(&map_x, &map_y, pcity, x, y);
	  assert(is_real);

	  if (map_get_tile(map_x, map_y)->worked) {
	    /* oops, inconsistent savegame; minimal fix: */
	    freelog(LOG_VERBOSE, "Inconsistent worked for %s (%d,%d), "
		    "converting to elvis", pcity->name, x, y);
	    pcity->ppl_elvis++;
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

    p=secfile_lookup_str(file, "player%d.c%d.improvements", plrno, i);

    /* Initialise list of improvements with City- and Building-wide
       equiv_ranges */
    improvement_status_init(pcity->improvements,
			    ARRAY_SIZE(pcity->improvements));

    /* Initialise city's vector of improvement effects. */
    ceff_vector_init(&pcity->effects);

    impr_type_iterate(x) {
      if (*p != '\0' && *p++=='1') {
        city_add_improvement(pcity,x);
      }
    } impr_type_iterate_end;

    init_worklist(&pcity->worklist);
    if (has_capability("worklists2", savefile_options)) {
      worklist_load(file, "player%d.c%d", plrno, i, &pcity->worklist);
    } else {
      worklist_load_old(file, "player%d.c%d.worklist%d",
			plrno, i, &pcity->worklist);
    }

    map_set_city(pcity->x, pcity->y, pcity);

    pcity->incite_revolt_cost = -1; /* flag value */

    city_list_insert_back(&plr->cities, pcity);
  }

  unit_list_init(&plr->units);
  nunits=secfile_lookup_int(file, "player%d.nunits", plrno);
  
  for(i=0; i<nunits; i++) { /* read the units */
    struct unit *punit;
    struct city *pcity;
    
    punit=fc_malloc(sizeof(struct unit));
    punit->id=secfile_lookup_int(file, "player%d.u%d.id", plrno, i);
    alloc_id(punit->id);
    idex_register_unit(punit);
    punit->owner=plrno;
    punit->x=secfile_lookup_int(file, "player%d.u%d.x", plrno, i);
    punit->y=secfile_lookup_int(file, "player%d.u%d.y", plrno, i);

    punit->veteran=secfile_lookup_bool(file, "player%d.u%d.veteran", plrno, i);
    punit->foul=secfile_lookup_bool_default(file, FALSE, "player%d.u%d.foul",
					   plrno, i);
    punit->homecity=secfile_lookup_int(file, "player%d.u%d.homecity",
				       plrno, i);

    if((pcity=find_city_by_id(punit->homecity)))
      unit_list_insert(&pcity->units_supported, punit);
    else
      punit->homecity=0;
    
    punit->type=secfile_lookup_int(file, "player%d.u%d.type", plrno, i);

    punit->moves_left=secfile_lookup_int(file, "player%d.u%d.moves", plrno, i);
    punit->fuel= secfile_lookup_int(file, "player%d.u%d.fuel", plrno, i);
    set_unit_activity(punit, secfile_lookup_int(file, "player%d.u%d.activity",plrno, i));
/* need to do this to assign/deassign settlers correctly -- Syela */
/* was punit->activity=secfile_lookup_int(file, "player%d.u%d.activity",plrno, i); */
    punit->activity_count=secfile_lookup_int(file, 
					     "player%d.u%d.activity_count",
					     plrno, i);
    punit->activity_target =
	secfile_lookup_int_default(file, (int) S_NO_SPECIAL,
				   "player%d.u%d.activity_target", plrno, i);

    punit->connecting=secfile_lookup_bool_default(file, FALSE,
						"player%d.u%d.connecting",
						plrno, i);

    punit->goto_dest_x=secfile_lookup_int(file, 
					  "player%d.u%d.goto_x", plrno,i);
    punit->goto_dest_y=secfile_lookup_int(file, 
					  "player%d.u%d.goto_y", plrno,i);
    punit->ai.control=secfile_lookup_bool(file, "player%d.u%d.ai", plrno,i);
    punit->ai.ai_role = AIUNIT_NONE;
    punit->ai.ferryboat = 0;
    punit->ai.passenger = 0;
    punit->ai.bodyguard = 0;
    punit->ai.charge = 0;
    punit->hp=secfile_lookup_int(file, "player%d.u%d.hp", plrno, i);
    punit->bribe_cost=-1;	/* flag value */
    
    punit->ord_map=secfile_lookup_int_default(file, 0, "player%d.u%d.ord_map",
					      plrno, i);
    punit->ord_city=secfile_lookup_int_default(file, 0, "player%d.u%d.ord_city",
					       plrno, i);
    punit->moved=secfile_lookup_bool_default(file, FALSE, "player%d.u%d.moved",
					    plrno, i);
    punit->paradropped=secfile_lookup_bool_default(file, FALSE, "player%d.u%d.paradropped",
                                                  plrno, i);
    punit->transported_by=secfile_lookup_int_default(file, -1, "player%d.u%d.transported_by",
                                                  plrno, i);
    /* Initialize upkeep values: these are hopefully initialized
       elsewhere before use (specifically, in city_support(); but
       fixme: check whether always correctly initialized?).
       Below is mainly for units which don't have homecity --
       otherwise these don't get initialized (and AI calculations
       etc may use junk values).
    */
    punit->unhappiness = 0;
    punit->upkeep      = 0;
    punit->upkeep_food = 0;
    punit->upkeep_gold = 0;

    /* load the goto route */
    {
      int len = secfile_lookup_int_default(file, 0, "player%d.u%d.goto_length", plrno, i);
      if (len > 0) {
	char *goto_buf, *goto_buf_ptr;
	struct goto_route *pgr = fc_malloc(sizeof(struct goto_route));
	pgr->pos = fc_malloc((len+1) * sizeof(struct map_position));
	pgr->first_index = 0;
	pgr->length = len+1;
	pgr->last_index = len;
	punit->pgr = pgr;

	/* get x coords */
	goto_buf = secfile_lookup_str(file, "player%d.u%d.goto_route_x", plrno, i);
	goto_buf_ptr = goto_buf;
	for (j = 0; j < len; j++) {
	  if (sscanf(goto_buf_ptr, "%d", &pgr->pos[j].x) == 0)
	    abort();
	  while (*goto_buf_ptr != ',') {
	    goto_buf_ptr++;
	    if (*goto_buf_ptr == '\0')
	      abort();
	  }
	  goto_buf_ptr++;
	}
	/* get y coords */
	goto_buf = secfile_lookup_str(file, "player%d.u%d.goto_route_y", plrno, i);
	goto_buf_ptr = goto_buf;
	for (j = 0; j < len; j++) {
	  if (sscanf(goto_buf_ptr, "%d", &pgr->pos[j].y) == 0)
	    abort();
	  while (*goto_buf_ptr != ',') {
	    goto_buf_ptr++;
	    if (*goto_buf_ptr == '\0')
	      abort();
	  }
	  goto_buf_ptr++;
	}
      } else {
	/* mark unused strings as read to avoid warnings */
	(void) secfile_lookup_str_default(file, "",
					  "player%d.u%d.goto_route_x", plrno,
					  i);
	(void) secfile_lookup_str_default(file, "",
					  "player%d.u%d.goto_route_y", plrno,
					  i);
	punit->pgr = NULL;
      }
    }

    {
      int range = unit_type(punit)->vision_range;
      square_iterate(punit->x, punit->y, range, x1, y1) {
	map_set_known(x1, y1, plr);
      } square_iterate_end;
    }

    /* allocate the unit's contribution to fog of war */
    if (unit_profits_of_watchtower(punit)
	&& map_has_special(punit->x, punit->y, S_FORTRESS))
      unfog_area(unit_owner(punit), punit->x, punit->y,
		 get_watchtower_vision(punit));
    else
      unfog_area(unit_owner(punit), punit->x, punit->y,
		 unit_type(punit)->vision_range);

    unit_list_insert_back(&plr->units, punit);

    unit_list_insert(&map_get_tile(punit->x, punit->y)->units, punit);
  }

  if (section_file_lookup(file, "player%d.attribute_block_length", plrno)) {
    int raw_length1, raw_length2, part_nr, parts;
    size_t quoted_length;
    char *quoted;

    raw_length1 =
	secfile_lookup_int(file, "player%d.attribute_block_length", plrno);
    if (plr->attribute_block.data) {
      free(plr->attribute_block.data);
      plr->attribute_block.data = NULL;
    }
    plr->attribute_block.data = fc_malloc(raw_length1);
    plr->attribute_block.length = raw_length1;

    quoted_length = secfile_lookup_int
	(file, "player%d.attribute_block_length_quoted", plrno);
    quoted = fc_malloc(quoted_length + 1);
    quoted[0] = 0;

    parts =
	secfile_lookup_int(file, "player%d.attribute_block_parts", plrno);

    for (part_nr = 0; part_nr < parts; part_nr++) {
      char *current = secfile_lookup_str(file,
					 "player%d.attribute_block_data.part%d",
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
			    struct section_file *file)
{
  int x,y,i;

  if (!plr->is_alive)
    whole_map_iterate(x, y) {
      map_change_seen(x, y, plr, +1);
    } whole_map_iterate_end;

  /* load map if:
     1) it from a fog of war build
     2) fog of war was on (otherwise the private map wasn't saved)
     3) is not from a "unit only" fog of war save file
  */
  if (secfile_lookup_int_default(file, -1, "game.fogofwar") != -1
      && game.fogofwar == TRUE
      && secfile_lookup_int_default(file, -1,"player%d.total_ncities", plrno) != -1
      && secfile_lookup_bool_default(file, TRUE, "game.save_private_map")
      && game.load_options.load_private_map) {
    LOAD_MAP_DATA(secfile_lookup_str(file, "player%d.map_t%03d", plrno, y),
		  map_get_player_tile(x, y, plr)->terrain =
		  char2terrain(ch));

    /* get 4-bit segments of 12-bit "special" field. */
    LOAD_MAP_DATA(secfile_lookup_str(file, "player%d.map_l%03d", plrno, y),
		  map_get_player_tile(x, y, plr)->special =
		  ascii_hex2bin(ch, 0));
    LOAD_MAP_DATA(secfile_lookup_str(file, "player%d.map_u%03d", plrno, y),
		  map_get_player_tile(x, y, plr)->special |=
		  ascii_hex2bin(ch, 1));
    LOAD_MAP_DATA(secfile_lookup_str_default
		  (file, NULL, "player%d.map_n%03d", plrno, y),
		  map_get_player_tile(x, y, plr)->special |=
		  ascii_hex2bin(ch, 2));

    /* get 4-bit segments of 16-bit "updated" field */
    LOAD_MAP_DATA(secfile_lookup_str
		  (file, "player%d.map_ua%03d", plrno, y),
		  map_get_player_tile(x, y, plr)->last_updated =
		  ascii_hex2bin(ch, 0));
    LOAD_MAP_DATA(secfile_lookup_str
		  (file, "player%d.map_ub%03d", plrno, y),
		  map_get_player_tile(x, y, plr)->last_updated |=
		  ascii_hex2bin(ch, 1));
    LOAD_MAP_DATA(secfile_lookup_str
		  (file, "player%d.map_uc%03d", plrno, y),
		  map_get_player_tile(x, y, plr)->last_updated |=
		  ascii_hex2bin(ch, 2));
    LOAD_MAP_DATA(secfile_lookup_str
		  (file, "player%d.map_ud%03d", plrno, y),
		  map_get_player_tile(x, y, plr)->last_updated |=
		  ascii_hex2bin(ch, 3));

    {
      int j;
      struct dumb_city *pdcity;
      i = secfile_lookup_int(file, "player%d.total_ncities", plrno);
      for (j = 0; j < i; j++) {
	pdcity = fc_malloc(sizeof(struct dumb_city));
	pdcity->id = secfile_lookup_int(file, "player%d.dc%d.id", plrno, j);
	x = secfile_lookup_int(file, "player%d.dc%d.x", plrno, j);
	y = secfile_lookup_int(file, "player%d.dc%d.y", plrno, j);
	sz_strlcpy(pdcity->name, secfile_lookup_str(file, "player%d.dc%d.name", plrno, j));
	pdcity->size = secfile_lookup_int(file, "player%d.dc%d.size", plrno, j);
	pdcity->has_walls = secfile_lookup_bool(file, "player%d.dc%d.has_walls", plrno, j);    
	pdcity->owner = secfile_lookup_int(file, "player%d.dc%d.owner", plrno, j);
	map_get_player_tile(x, y, plr)->city = pdcity;
	alloc_id(pdcity->id);
      }
    }

    /* This shouldn't be neccesary if the savegame was consistent, but there
       is a bug in some pre-1.11 savegames. Anyway, it can't hurt */
    whole_map_iterate(x, y) {
      if (map_get_known_and_seen(x, y, get_player(plrno))) {
	update_tile_knowledge(plr, x, y);
	reality_check_city(plr, x, y);
	if (map_get_city(x, y)) {
	  update_dumb_city(plr, map_get_city(x, y));
	}
      }
    } whole_map_iterate_end;

  } else {
    /* We have an old savegame or fog of war was turned off; the
       players private knowledge is set to be what he could see
       without fog of war */
    whole_map_iterate(x, y) {
      if (map_get_known(x, y, plr)) {
	struct city *pcity = map_get_city(x, y);
	update_player_tile_last_seen(plr, x, y);
	update_tile_knowledge(plr, x, y);
	if (pcity)
	  update_dumb_city(plr, pcity);
      }
    } whole_map_iterate_end;
  }
}

/***************************************************************
Save the worklist elements specified by path, given the arguments
plrno and wlinx, from the worklist pointed to by pwl.
***************************************************************/
static void worklist_save(struct section_file *file,
			  char *path, int plrno, int wlinx,
			  struct worklist *pwl)
{
  char efpath[64];
  char idpath[64];
  int i;

  sz_strlcpy(efpath, path);
  sz_strlcat(efpath, ".wlef%d");
  sz_strlcpy(idpath, path);
  sz_strlcat(idpath, ".wlid%d");

  for (i = 0; i < MAX_LEN_WORKLIST; i++) {
    secfile_insert_int(file, pwl->wlefs[i], efpath, plrno, wlinx, i);
    secfile_insert_int(file, pwl->wlids[i], idpath, plrno, wlinx, i);
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

  secfile_insert_str(file, plr->name, "player%d.name", plrno);
  secfile_insert_str(file, plr->username, "player%d.username", plrno);
  secfile_insert_int(file, plr->nation, "player%d.race", plrno);
  secfile_insert_int(file, plr->government, "player%d.government", plrno);
  secfile_insert_int(file, plr->embassy, "player%d.embassy", plrno);

  secfile_insert_int(file, plr->city_style, "player%d.city_style", plrno);
  secfile_insert_bool(file, plr->is_male, "player%d.is_male", plrno);
  secfile_insert_bool(file, plr->is_alive, "player%d.is_alive", plrno);
  secfile_insert_bool(file, plr->ai.control, "player%d.ai.control", plrno);
  secfile_insert_int(file, plr->ai.tech_goal, "player%d.ai.tech_goal", plrno);
  secfile_insert_int(file, plr->ai.skill_level,
		     "player%d.ai.skill_level", plrno);
  secfile_insert_int(file, plr->ai.barbarian_type, "player%d.ai.is_barbarian", plrno);
  secfile_insert_int(file, plr->economic.gold, "player%d.gold", plrno);
  secfile_insert_int(file, plr->economic.tax, "player%d.tax", plrno);
  secfile_insert_int(file, plr->economic.science, "player%d.science", plrno);
  secfile_insert_int(file, plr->economic.luxury, "player%d.luxury", plrno);

  secfile_insert_int(file,plr->future_tech,"player%d.futuretech", plrno);

  secfile_insert_int(file, plr->research.bulbs_researched, 
		     "player%d.researched", plrno);
  secfile_insert_int(file, plr->research.techs_researched,
		     "player%d.researchpoints", plrno);
  secfile_insert_int(file, plr->research.researching,
		     "player%d.researching", plrno);  

  secfile_insert_bool(file, plr->capital, "player%d.capital", plrno);
  secfile_insert_int(file, plr->revolution, "player%d.revolution", plrno);

  for(i=0; i<game.num_tech_types; i++)
    invs[i]=(get_invention(plr, i)==TECH_KNOWN) ? '1' : '0';
  invs[i]='\0';
  secfile_insert_str(file, invs, "player%d.invs", plrno);

  secfile_insert_int(file, plr->reputation, "player%d.reputation", plrno);
  for (i = 0; i < MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
    secfile_insert_int(file, plr->diplstates[i].type,
		       "player%d.diplstate%d.type", plrno, i);
    secfile_insert_int(file, plr->diplstates[i].turns_left,
		       "player%d.diplstate%d.turns_left", plrno, i);
    secfile_insert_int(file, plr->diplstates[i].has_reason_to_cancel,
		       "player%d.diplstate%d.has_reason_to_cancel", plrno, i);
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

  secfile_insert_int(file, unit_list_size(&plr->units), "player%d.nunits", 
		     plrno);
  secfile_insert_int(file, city_list_size(&plr->cities), "player%d.ncities", 
		     plrno);

  i = -1;
  unit_list_iterate(plr->units, punit) {
    i++;
    secfile_insert_int(file, punit->id, "player%d.u%d.id", plrno, i);
    secfile_insert_int(file, punit->x, "player%d.u%d.x", plrno, i);
    secfile_insert_int(file, punit->y, "player%d.u%d.y", plrno, i);
    secfile_insert_bool(file, punit->veteran, "player%d.u%d.veteran", 
				plrno, i);
    secfile_insert_bool(file, punit->foul, "player%d.u%d.foul", 
				plrno, i);
    secfile_insert_int(file, punit->hp, "player%d.u%d.hp", plrno, i);
    secfile_insert_int(file, punit->homecity, "player%d.u%d.homecity",
				plrno, i);
    secfile_insert_int(file, punit->type, "player%d.u%d.type",
				plrno, i);
    secfile_insert_int(file, punit->activity, "player%d.u%d.activity",
				plrno, i);
    secfile_insert_int(file, punit->activity_count, 
				"player%d.u%d.activity_count",
				plrno, i);
    secfile_insert_int(file, punit->activity_target, 
				"player%d.u%d.activity_target",
				plrno, i);
    secfile_insert_bool(file, punit->connecting, 
				"player%d.u%d.connecting",
				plrno, i);
    secfile_insert_int(file, punit->moves_left, "player%d.u%d.moves",
		                plrno, i);
    secfile_insert_int(file, punit->fuel, "player%d.u%d.fuel",
		                plrno, i);

    secfile_insert_int(file, punit->goto_dest_x, "player%d.u%d.goto_x", plrno, i);
    secfile_insert_int(file, punit->goto_dest_y, "player%d.u%d.goto_y", plrno, i);
    secfile_insert_bool(file, punit->ai.control, "player%d.u%d.ai", plrno, i);
    secfile_insert_int(file, punit->ord_map, "player%d.u%d.ord_map", plrno, i);
    secfile_insert_int(file, punit->ord_city, "player%d.u%d.ord_city", plrno, i);
    secfile_insert_bool(file, punit->moved, "player%d.u%d.moved", plrno, i);
    secfile_insert_bool(file, punit->paradropped, "player%d.u%d.paradropped", plrno, i);
    secfile_insert_int(file, punit->transported_by,
		       "player%d.u%d.transported_by", plrno, i);
    if (punit->pgr && punit->pgr->first_index != punit->pgr->last_index) {
      struct goto_route *pgr = punit->pgr;
      int index = pgr->first_index;
      int len = 0;
      while (pgr && index != pgr->last_index) {
	len++;
	index = (index + 1) % pgr->length;
      }
      assert(len > 0);
      secfile_insert_int(file, len, "player%d.u%d.goto_length", plrno, i);
      /* assumption about the chars per map position */
      assert(MAP_MAX_HEIGHT < 1000 && MAP_MAX_WIDTH < 1000);
      {
	char *goto_buf = fc_malloc(4 * len + 1);
	char *goto_buf_ptr = goto_buf;
	index = pgr->first_index;
	while (index != pgr->last_index) {
	  goto_buf_ptr += sprintf(goto_buf_ptr, "%d,", pgr->pos[index].x);
	  index = (index + 1) % pgr->length;
	}
	*goto_buf_ptr = '\0';
	secfile_insert_str(file, goto_buf, "player%d.u%d.goto_route_x", plrno, i);

	goto_buf_ptr = goto_buf;
	index = pgr->first_index;
	while (index != pgr->last_index) {
	  goto_buf_ptr += sprintf(goto_buf_ptr, "%d,", pgr->pos[index].y);
	  index = (index + 1) % pgr->length;
	}
	*goto_buf_ptr = '\0';
	secfile_insert_str(file, goto_buf, "player%d.u%d.goto_route_y", plrno, i);
      }
    } else {
      secfile_insert_int(file, 0, "player%d.u%d.goto_length", plrno, i);
      secfile_insert_str(file, "", "player%d.u%d.goto_route_x", plrno, i);
      secfile_insert_str(file, "", "player%d.u%d.goto_route_y", plrno, i);
    }
  }
  unit_list_iterate_end;

  i = -1;
  city_list_iterate(plr->cities, pcity) {
    int j, x, y;
    char buf[512];

    i++;
    secfile_insert_int(file, pcity->id, "player%d.c%d.id", plrno, i);
    secfile_insert_int(file, pcity->x, "player%d.c%d.x", plrno, i);
    secfile_insert_int(file, pcity->y, "player%d.c%d.y", plrno, i);
    secfile_insert_str(file, pcity->name, "player%d.c%d.name", plrno, i);
    secfile_insert_int(file, pcity->original, "player%d.c%d.original", 
		       plrno, i);
    secfile_insert_int(file, pcity->size, "player%d.c%d.size", plrno, i);
    secfile_insert_int(file, pcity->steal, "player%d.c%d.steal", plrno, i);
    secfile_insert_int(file, pcity->ppl_elvis, "player%d.c%d.nelvis", plrno, i);
    secfile_insert_int(file, pcity->ppl_scientist, "player%d.c%d.nscientist", 
		       plrno, i);
    secfile_insert_int(file, pcity->ppl_taxman, "player%d.c%d.ntaxman", plrno, i);

    for (j = 0; j < NUM_TRADEROUTES; j++)
      secfile_insert_int(file, pcity->trade[j], "player%d.c%d.traderoute%d", 
			 plrno, i, j);
    
    secfile_insert_int(file, pcity->food_stock, "player%d.c%d.food_stock", 
		       plrno, i);
    secfile_insert_int(file, pcity->shield_stock, "player%d.c%d.shield_stock", 
		       plrno, i);
    secfile_insert_int(file, pcity->turn_last_built,
		       "player%d.c%d.turn_last_built", plrno, i);
    secfile_insert_int(file, pcity->turn_changed_target,
		       "player%d.c%d.turn_changed_target", plrno, i);
    secfile_insert_int(file, pcity->changed_from_id,
		       "player%d.c%d.changed_from_id", plrno, i);
    secfile_insert_bool(file, pcity->changed_from_is_unit,
		       "player%d.c%d.changed_from_is_unit", plrno, i);
    secfile_insert_int(file, pcity->before_change_shields,
		       "player%d.c%d.before_change_shields", plrno, i);
    secfile_insert_int(file, pcity->disbanded_shields,
		       "player%d.c%d.disbanded_shields", plrno, i);
    secfile_insert_int(file, pcity->caravan_shields,
		       "player%d.c%d.caravan_shields", plrno, i);

    secfile_insert_int(file, pcity->anarchy, "player%d.c%d.anarchy", plrno,i);
    secfile_insert_int(file, pcity->rapture, "player%d.c%d.rapture", plrno,i);
    secfile_insert_bool(file, pcity->was_happy, "player%d.c%d.was_happy", plrno,i);
    secfile_insert_int(file, pcity->did_buy, "player%d.c%d.did_buy", plrno,i);
    secfile_insert_bool(file, pcity->did_sell, "player%d.c%d.did_sell", plrno,i);
    secfile_insert_bool(file, pcity->airlift, "player%d.c%d.airlift", plrno,i);

    /* for auto_attack */
    secfile_insert_int(file, pcity->city_options,
		       "player%d.c%d.options", plrno, i);
    
    j=0;
    for(y=0; y<CITY_MAP_SIZE; y++) {
      for(x=0; x<CITY_MAP_SIZE; x++) {
	switch (get_worker_city(pcity, x, y)) {
	  case C_TILE_EMPTY:       buf[j++] = '0'; break;
	  case C_TILE_WORKER:      buf[j++] = '1'; break;
	  case C_TILE_UNAVAILABLE: buf[j++] = '2'; break;
	}
      }
    }
    buf[j]='\0';
    secfile_insert_str(file, buf, "player%d.c%d.workers", plrno, i);

    secfile_insert_bool(file, pcity->is_building_unit, 
		       "player%d.c%d.is_building_unit", plrno, i);
    secfile_insert_int(file, pcity->currently_building, 
		       "player%d.c%d.currently_building", plrno, i);

    impr_type_iterate(id) {
      buf[id] = (pcity->improvements[id] != I_NONE) ? '1' : '0';
    } impr_type_iterate_end;
    buf[game.num_impr_types] = '\0';
    secfile_insert_str(file, buf, "player%d.c%d.improvements", plrno, i);

    worklist_save(file, "player%d.c%d", plrno, i, &pcity->worklist);
  }
  city_list_iterate_end;

  /********** Put the players private map **********/
 /* Otherwise the player can see all, and there's no reason to save the private map. */
  if (game.fogofwar
      && game.save_options.save_private_map) {

    /* put the terrain type */
    SAVE_PLAYER_MAP_DATA(file,
			 terrain_chars[map_get_player_tile
				       (x, y, plr)->terrain],
			 "player%d.map_t%03d", plrno);

    /* put 4-bit segments of 12-bit "special flags" field */
    SAVE_PLAYER_MAP_DATA(file,
			 bin2ascii_hex(map_get_player_tile(x, y, plr)->
				       special, 0), "player%d.map_l%03d",
			 plrno);
    SAVE_PLAYER_MAP_DATA(file,
			 bin2ascii_hex(map_get_player_tile(x, y, plr)->
				       special, 1), "player%d.map_u%03d",
			 plrno);
    SAVE_PLAYER_MAP_DATA(file,
			 bin2ascii_hex(map_get_player_tile(x, y, plr)->
				       special, 2), "player%d.map_n%03d",
			 plrno);

    /* put 4-bit segments of 16-bit "updated" field */
    SAVE_PLAYER_MAP_DATA(file,
			 bin2ascii_hex(map_get_player_tile
				       (x, y, plr)->last_updated, 0),
			 "player%d.map_ua%03d", plrno);
    SAVE_PLAYER_MAP_DATA(file,
			 bin2ascii_hex(map_get_player_tile
				       (x, y, plr)->last_updated, 1),
			 "player%d.map_ub%03d", plrno);
    SAVE_PLAYER_MAP_DATA(file,
			 bin2ascii_hex(map_get_player_tile
				       (x, y, plr)->last_updated, 2),
			 "player%d.map_uc%03d", plrno);
    SAVE_PLAYER_MAP_DATA(file,
			 bin2ascii_hex(map_get_player_tile
				       (x, y, plr)->last_updated, 3),
			 "player%d.map_ud%03d", plrno);

    if (TRUE) {
      struct dumb_city *pdcity;
      i = 0;
      
      whole_map_iterate(x, y) {
	if ((pdcity = map_get_player_tile(x, y, plr)->city)) {
	  secfile_insert_int(file, pdcity->id, "player%d.dc%d.id", plrno,
			     i);
	  secfile_insert_int(file, x, "player%d.dc%d.x", plrno, i);
	  secfile_insert_int(file, y, "player%d.dc%d.y", plrno, i);
	  secfile_insert_str(file, pdcity->name, "player%d.dc%d.name",
			     plrno, i);
	  secfile_insert_int(file, pdcity->size, "player%d.dc%d.size",
			     plrno, i);
	  secfile_insert_bool(file, pdcity->has_walls,
			     "player%d.dc%d.has_walls", plrno, i);
	  secfile_insert_int(file, pdcity->owner, "player%d.dc%d.owner",
			     plrno, i);
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
		       "player%d.attribute_block_length", plrno);
    secfile_insert_int(file, strlen(quoted),
		       "player%d.attribute_block_length_quoted", plrno);

    parts = (strlen(quoted) - 1) / PART_SIZE + 1;
    bytes_left = strlen(quoted);

    secfile_insert_int(file, parts,
		       "player%d.attribute_block_parts", plrno);

    for (current_part_nr = 0; current_part_nr < parts; current_part_nr++) {
      size_t size_of_current_part = MIN(bytes_left, PART_SIZE);

      assert(bytes_left);

      memcpy(part, quoted + PART_SIZE * current_part_nr,
	     size_of_current_part);
      part[size_of_current_part] = 0;
      secfile_insert_str(file, part,
			 "player%d.attribute_block_data.part%d", plrno,
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

  whole_map_iterate(x, y) {
    j = 0;
    unit_list_iterate(map_get_tile(x, y)->units, punit) {
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
      unit_list_sort_ord_city(&pcity->units_supported);
    }
    city_list_iterate_end;
  } players_iterate_end;

  whole_map_iterate(x, y) {
    unit_list_sort_ord_map(&map_get_tile(x, y)->units);
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
	int map_x, map_y;
	bool is_real;

	pcity->ppl_elvis++;
	set_worker_city(pcity, x, y, C_TILE_UNAVAILABLE);
	freelog(LOG_DEBUG, "Worked tile was unavailable!");

	is_real = city_map_to_map(&map_x, &map_y, pcity, x, y);
	assert(is_real);

	map_city_radius_iterate(map_x, map_y, x2, y2) {
	  struct city *pcity2 = map_get_city(x2, y2);
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
  int i;
  enum server_states tmp_server_state;
  char *savefile_options;
  char *string;

  game.version = secfile_lookup_int_default(file, 0, "game.version");
  tmp_server_state = (enum server_states)
    secfile_lookup_int_default(file, RUN_GAME_STATE, "game.server_state");

  savefile_options = secfile_lookup_str(file, "savefile.options");

  /* we require at least version 1.9.0 */
  if (10900 > game.version) {
    freelog(LOG_FATAL,
	    _("Savegame too old, at least version 1.9.0 required."));
    exit(EXIT_FAILURE);
  }

  if (game.load_options.load_settings) {
    sz_strlcpy(srvarg.metaserver_info_line,
	       secfile_lookup_str_default(file, default_meta_server_info_string(),
					  "game.metastring"));
    sz_strlcpy(srvarg.metaserver_addr,
	       secfile_lookup_str_default(file, DEFAULT_META_SERVER_ADDR,
					  "game.metaserver"));
    meta_addr_split();

    game.gold          = secfile_lookup_int(file, "game.gold");
    game.tech          = secfile_lookup_int(file, "game.tech");
    game.skill_level   = secfile_lookup_int(file, "game.skill_level");
    if (game.skill_level==0)
      game.skill_level = GAME_OLD_DEFAULT_SKILL_LEVEL;

    game.timeout       = secfile_lookup_int(file, "game.timeout");
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

    game.end_year      = secfile_lookup_int(file, "game.end_year");
    game.researchcost  = secfile_lookup_int_default(file, 0, "game.researchcost");
    if (game.researchcost == 0)
      game.researchcost = secfile_lookup_int(file, "game.techlevel");

    game.year          = secfile_lookup_int(file, "game.year");

    if (has_capability("turn", savefile_options)) {
      game.turn = secfile_lookup_int(file, "game.turn");
    } else {
      game.turn = -2;
    }

    game.min_players   = secfile_lookup_int(file, "game.min_players");
    game.max_players   = secfile_lookup_int(file, "game.max_players");
    game.nplayers      = secfile_lookup_int(file, "game.nplayers");
    game.globalwarming = secfile_lookup_int(file, "game.globalwarming");
    game.warminglevel  = secfile_lookup_int(file, "game.warminglevel");
    game.nuclearwinter = secfile_lookup_int_default(file, 0, "game.nuclearwinter");
    game.coolinglevel  = secfile_lookup_int_default(file, 8, "game.coolinglevel");
    game.notradesize   = secfile_lookup_int_default(file, 0, "game.notradesize");
    game.fulltradesize = secfile_lookup_int_default(file, 1, "game.fulltradesize");
    game.unhappysize   = secfile_lookup_int(file, "game.unhappysize");
    game.angrycitizen  = secfile_lookup_int_default(file, 0, "game.angrycitizen");

    if (game.version >= 10100) {
      game.cityfactor  = secfile_lookup_int(file, "game.cityfactor");
      game.diplcost    = secfile_lookup_int(file, "game.diplcost");
      game.freecost    = secfile_lookup_int(file, "game.freecost");
      game.conquercost = secfile_lookup_int(file, "game.conquercost");
      game.foodbox     = secfile_lookup_int(file, "game.foodbox");
      game.techpenalty = secfile_lookup_int(file, "game.techpenalty");
      game.razechance  = secfile_lookup_int(file, "game.razechance");

      /* suppress warnings about unused entries in old savegames: */
      (void) section_file_lookup(file, "game.rail_food");
      (void) section_file_lookup(file, "game.rail_prod");
      (void) section_file_lookup(file, "game.rail_trade");
      (void) section_file_lookup(file, "game.farmfood");
    }
    if (game.version >= 10300) {
      game.civstyle = secfile_lookup_int_default(file, 0, "game.civstyle");
      game.save_nturns = secfile_lookup_int(file, "game.save_nturns");
    }

    game.citymindist  = secfile_lookup_int_default(file,
      GAME_DEFAULT_CITYMINDIST, "game.citymindist");

    game.rapturedelay  = secfile_lookup_int_default(file,
      GAME_DEFAULT_RAPTUREDELAY, "game.rapturedelay");

    if (has_capability("watchtower", savefile_options)) {
      game.watchtower_extra_vision =
	  secfile_lookup_int(file, "game.watchtower_extra_vision");
      game.watchtower_vision =
	  secfile_lookup_int(file, "game.watchtower_vision");
    } else {
      game.watchtower_extra_vision = 0;
      game.watchtower_vision = 1;
    }

    sz_strlcpy(game.save_name,
	       secfile_lookup_str_default(file, GAME_DEFAULT_SAVE_NAME,
					  "game.save_name"));

    game.aifill = secfile_lookup_int_default(file, 0, "game.aifill");

    game.scorelog = secfile_lookup_bool_default(file, FALSE, "game.scorelog");

    game.fogofwar = secfile_lookup_bool_default(file, FALSE, "game.fogofwar");
    game.fogofwar_old = game.fogofwar;
  
    game.civilwarsize =
      secfile_lookup_int_default(file, GAME_DEFAULT_CIVILWARSIZE,
				 "game.civilwarsize");
  
    if(has_capability("diplchance_percent", savefile_options)) {
      game.diplchance = secfile_lookup_int_default(file, game.diplchance,
						   "game.diplchance");
    } else {
      game.diplchance = secfile_lookup_int_default(file, 3, /* old default */
						   "game.diplchance");
      if (game.diplchance < 2) {
	game.diplchance = GAME_MAX_DIPLCHANCE;
      } else if (game.diplchance > 10) {
	game.diplchance = GAME_MIN_DIPLCHANCE;
      } else {
	game.diplchance = 100 - (10 * (game.diplchance - 1));
      }
    }
    game.aqueductloss = secfile_lookup_int_default(file, game.aqueductloss,
						   "game.aqueductloss");
    game.killcitizen = secfile_lookup_int_default(file, game.killcitizen,
						  "game.killcitizen");
    game.savepalace = secfile_lookup_bool_default(file,game.savepalace,
						"game.savepalace");
    game.turnblock = secfile_lookup_bool_default(file,game.turnblock,
						"game.turnblock");
    game.fixedlength = secfile_lookup_bool_default(file,game.fixedlength,
						  "game.fixedlength");
    game.barbarianrate = secfile_lookup_int_default(file, game.barbarianrate,
						    "game.barbarians");
    game.onsetbarbarian = secfile_lookup_int_default(file, game.onsetbarbarian,
						     "game.onsetbarbs");
    game.nbarbarians = 0; /* counted in player_load for compatibility with 
			     1.10.0 */
    game.occupychance = secfile_lookup_int_default(file, game.occupychance,
						   "game.occupychance");
    game.randseed = secfile_lookup_int_default(file, game.randseed,
					       "game.randseed");

    if(game.civstyle == 1) {
      string = "civ1";
    } else {
      string = "default";
      game.civstyle = GAME_DEFAULT_CIVSTYLE;
    }

    if (!has_capability("rulesetdir", savefile_options)) {
      char *str2, *str =
	  secfile_lookup_str_default(file, "default", "game.ruleset.techs");

      if (strcmp("classic",
		 secfile_lookup_str_default(file, "default",
					    "game.ruleset.terrain")) == 0) {
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

      T("game.ruleset.units");
      T("game.ruleset.buildings");
      T("game.ruleset.terrain");
      T("game.ruleset.governments");
      T("game.ruleset.nations");
      T("game.ruleset.cities");
      T("game.ruleset.game");
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

    game.spacerace = secfile_lookup_bool_default(file, game.spacerace,
						"game.spacerace");

    game.auto_ai_toggle = secfile_lookup_bool_default(file, game.auto_ai_toggle,
						     "game.auto_ai_toggle");

    game.heating=0;
    game.cooling=0;

    load_rulesets();
  }

  {
    if (game.version >= 10300) {
      if (game.load_options.load_settings) {
	game.settlers = secfile_lookup_int(file, "game.settlers");
	game.explorer = secfile_lookup_int(file, "game.explorer");
	game.dispersion =
	  secfile_lookup_int_default(file, GAME_DEFAULT_DISPERSION, "game.dispersion");
      }

      map.riches = secfile_lookup_int(file, "map.riches");
      map.huts = secfile_lookup_int(file, "map.huts");
      map.generator = secfile_lookup_int(file, "map.generator");
      map.seed = secfile_lookup_int(file, "map.seed");
      map.landpercent = secfile_lookup_int(file, "map.landpercent");
      map.grasssize =
	secfile_lookup_int_default(file, MAP_DEFAULT_GRASS, "map.grasssize");
      map.swampsize = secfile_lookup_int(file, "map.swampsize");
      map.deserts = secfile_lookup_int(file, "map.deserts");
      map.riverlength = secfile_lookup_int(file, "map.riverlength");
      map.mountains = secfile_lookup_int(file, "map.mountains");
      map.forestsize = secfile_lookup_int(file, "map.forestsize");
      map.have_huts = secfile_lookup_bool_default(file, TRUE, "map.have_huts");

      if (has_capability("startoptions", savefile_options)) {
	map.xsize = secfile_lookup_int(file, "map.width");
	map.ysize = secfile_lookup_int(file, "map.height");
      } else {
	/* old versions saved with these names in PRE_GAME_STATE: */
	map.xsize = secfile_lookup_int(file, "map.xsize");
	map.ysize = secfile_lookup_int(file, "map.ysize");
      }

      if (tmp_server_state==PRE_GAME_STATE
	  && map.generator == 0
	  && !has_capability("map_editor",savefile_options)) {
	/* generator 0 = map done with map editor */
	/* aka a "scenario" */
        if (has_capability("specials",savefile_options)) {
          map_load(file);
	  map.fixed_start_positions = TRUE;
          return;
        }
        map_tiles_load(file);
        if (has_capability("riversoverlay",savefile_options)) {
	  map_rivers_overlay_load(file);
	}
        if (has_capability("startpos",savefile_options)) {
          map_startpos_load(file);
	  map.fixed_start_positions = TRUE;
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
      && secfile_lookup_bool_default(file, TRUE, "game.save_random")
      && game.load_options.load_random) {
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
  }


  game.is_new_game = !secfile_lookup_bool_default(file, TRUE, "game.save_players");

  if (!game.is_new_game) { /* If new game, this is done in srv_main.c */
    /* Initialise lists of improvements with World and Island equiv_ranges */
    improvement_status_init(game.improvements,
			    ARRAY_SIZE(game.improvements));

    /* Blank vector of effects with world-wide range. */
    geff_vector_free(&game.effects);

    /* Blank vector of destroyed effects. */
    ceff_vector_free(&game.destroyed_effects);
  }

  map_load(file);

  if (!game.is_new_game && game.load_options.load_players) {
    /* destroyed wonders: */
    string = secfile_lookup_str_default(file, "", "game.destroyed_wonders");
    impr_type_iterate(i) {
      if (string[i] == '\0') {
	goto out;
      }
      if (string[i] == '1') {
	game.global_wonders[i] = -1; /* destroyed! */
      }
    } impr_type_iterate_end;
  out:

    for(i=0; i<game.nplayers; i++) {
      player_load(&game.players[i], i, file); 
    }
    /* Since the cities must be placed on the map to put them on the
       player map we do this afterwards */
    for(i=0; i<game.nplayers; i++) {
      player_map_load(&game.players[i], i, file); 
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
      vision = secfile_lookup_str_default(file, NULL, "player%d.gives_shared_vision",
					  plrno);
      if (vision)
	for (i=0; i < MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++)
	  if (vision[i] == '1')
	    give_shared_vision(pplayer, get_player(i));
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
    game.nplayers = 0;
  }

  if (!game.is_new_game) {
    /* Set active city improvements/wonders and their effects */
    update_all_effects();
  }

  game.player_idx=0;
  game.player_ptr=&game.players[0];  

  return;
}

/***************************************************************
...
***************************************************************/
void game_save(struct section_file *file)
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
  secfile_insert_int(file, (int) (game.is_new_game ? server_state :
				  RUN_GAME_STATE), "game.server_state");
  
  secfile_insert_str(file, srvarg.metaserver_info_line, "game.metastring");
  secfile_insert_str(file, meta_addr_port(), "game.metaserver");
  
  sz_strlcpy(options, SAVEFILE_OPTIONS);
  if (game.is_new_game) {
    if (map.num_start_positions>0) {
      sz_strlcat(options, " startpos");
    }
    if (map.have_specials) {
      sz_strlcat(options, " specials");
    }
    if (map.have_rivers_overlay && !map.have_specials) {
      sz_strlcat(options, " riversoverlay");
    }
  }
  secfile_insert_str(file, options, "savefile.options");

  secfile_insert_int(file, game.gold, "game.gold");
  secfile_insert_int(file, game.tech, "game.tech");
  secfile_insert_int(file, game.skill_level, "game.skill_level");
  secfile_insert_int(file, game.timeout, "game.timeout");
  secfile_insert_int(file, game.timeoutint, "game.timeoutint");
  secfile_insert_int(file, game.timeoutintinc, "game.timeoutintinc");
  secfile_insert_int(file, game.timeoutinc, "game.timeoutinc");
  secfile_insert_int(file, game.timeoutincmult, "game.timeoutincmult"); 
  secfile_insert_int(file, game.timeoutcounter, "game.timeoutcounter"); 
  secfile_insert_int(file, game.end_year, "game.end_year");
  secfile_insert_int(file, game.year, "game.year");
  secfile_insert_int(file, game.turn, "game.turn");
  secfile_insert_int(file, game.researchcost, "game.researchcost");
  secfile_insert_int(file, game.min_players, "game.min_players");
  secfile_insert_int(file, game.max_players, "game.max_players");
  secfile_insert_int(file, game.nplayers, "game.nplayers");
  secfile_insert_int(file, game.globalwarming, "game.globalwarming");
  secfile_insert_int(file, game.warminglevel, "game.warminglevel");
  secfile_insert_int(file, game.nuclearwinter, "game.nuclearwinter");
  secfile_insert_int(file, game.coolinglevel, "game.coolinglevel");
  secfile_insert_int(file, game.notradesize, "game.notradesize");
  secfile_insert_int(file, game.fulltradesize, "game.fulltradesize");
  secfile_insert_int(file, game.unhappysize, "game.unhappysize");
  secfile_insert_int(file, game.angrycitizen, "game.angrycitizen");
  secfile_insert_int(file, game.cityfactor, "game.cityfactor");
  secfile_insert_int(file, game.citymindist, "game.citymindist");
  secfile_insert_int(file, game.civilwarsize, "game.civilwarsize");
  secfile_insert_int(file, game.rapturedelay, "game.rapturedelay");
  secfile_insert_int(file, game.diplcost, "game.diplcost");
  secfile_insert_int(file, game.freecost, "game.freecost");
  secfile_insert_int(file, game.conquercost, "game.conquercost");
  secfile_insert_int(file, game.foodbox, "game.foodbox");
  secfile_insert_int(file, game.techpenalty, "game.techpenalty");
  secfile_insert_int(file, game.razechance, "game.razechance");
  secfile_insert_int(file, game.civstyle, "game.civstyle");
  secfile_insert_int(file, game.save_nturns, "game.save_nturns");
  secfile_insert_str(file, game.save_name, "game.save_name");
  secfile_insert_int(file, game.aifill, "game.aifill");
  secfile_insert_bool(file, game.scorelog, "game.scorelog");
  secfile_insert_bool(file, game.fogofwar, "game.fogofwar");
  secfile_insert_bool(file, game.spacerace, "game.spacerace");
  secfile_insert_bool(file, game.auto_ai_toggle, "game.auto_ai_toggle");
  secfile_insert_int(file, game.diplchance, "game.diplchance");
  secfile_insert_int(file, game.aqueductloss, "game.aqueductloss");
  secfile_insert_int(file, game.killcitizen, "game.killcitizen");
  secfile_insert_bool(file, game.turnblock, "game.turnblock");
  secfile_insert_bool(file, game.savepalace, "game.savepalace");
  secfile_insert_bool(file, game.fixedlength, "game.fixedlength");
  secfile_insert_int(file, game.barbarianrate, "game.barbarians");
  secfile_insert_int(file, game.onsetbarbarian, "game.onsetbarbs");
  secfile_insert_int(file, game.occupychance, "game.occupychance");
  secfile_insert_str(file, game.demography, "game.demography");
  secfile_insert_int(file, game.watchtower_vision, "game.watchtower_vision");
  secfile_insert_int(file, game.watchtower_extra_vision, "game.watchtower_extra_vision");

  if (TRUE) {
    /* Now always save these, so the server options reflect the
     * actual values used at the start of the game.
     * The first two used to be saved as "map.xsize" and "map.ysize"
     * when PRE_GAME_STATE, but I'm standardizing on width,height --dwp
     */
    secfile_insert_int(file, map.xsize, "map.width");
    secfile_insert_int(file, map.ysize, "map.height");
    secfile_insert_int(file, game.settlers, "game.settlers");
    secfile_insert_int(file, game.explorer, "game.explorer");
    secfile_insert_int(file, game.dispersion, "game.dispersion");
    secfile_insert_int(file, map.seed, "map.seed");
    secfile_insert_int(file, map.landpercent, "map.landpercent");
    secfile_insert_int(file, map.riches, "map.riches");
    secfile_insert_int(file, map.swampsize, "map.swampsize");
    secfile_insert_int(file, map.deserts, "map.deserts");
    secfile_insert_int(file, map.riverlength, "map.riverlength");
    secfile_insert_int(file, map.mountains, "map.mountains");
    secfile_insert_int(file, map.forestsize, "map.forestsize");
    secfile_insert_int(file, map.huts, "map.huts");
    secfile_insert_int(file, map.generator, "map.generator");
    secfile_insert_bool(file, map.have_huts, "map.have_huts");
  } 

  secfile_insert_int(file, game.randseed, "game.randseed");
  
  if (myrand_is_init() && game.save_options.save_random) {
    RANDOM_STATE rstate = get_myrand_state();
    secfile_insert_int(file, 1, "game.save_random");
    assert(rstate.is_init);

    secfile_insert_int(file, rstate.j, "random.index_J");
    secfile_insert_int(file, rstate.k, "random.index_K");
    secfile_insert_int(file, rstate.x, "random.index_X");

    for(i=0;i<8;i++) {
      char name[20], vec[100];
      my_snprintf(name, sizeof(name), "random.table%d", i);
      my_snprintf(vec, sizeof(vec),
		  "%8x %8x %8x %8x %8x %8x %8x", rstate.v[7*i],
		  rstate.v[7*i+1], rstate.v[7*i+2], rstate.v[7*i+3],
		  rstate.v[7*i+4], rstate.v[7*i+5], rstate.v[7*i+6]);
      secfile_insert_str(file, vec, name);
    }
  } else {
    secfile_insert_int(file, 0, "game.save_random");
  }

  secfile_insert_str(file, game.rulesetdir, "game.rulesetdir");

  if (!map_is_empty())
    map_save(file);
  
  if ((server_state==PRE_GAME_STATE) && game.is_new_game) {
    return; /* want to save scenarios as well */
  }

  secfile_insert_bool(file, game.save_options.save_players, "game.save_players");
  if (game.save_options.save_players) {
    /* destroyed wonders: */
    impr_type_iterate(i) {
      if (is_wonder(i) && game.global_wonders[i]!=0
	  && !find_city_by_id(game.global_wonders[i])) {
	temp[i] = '1';
      } else {
	temp[i] = '0';
      }
    } impr_type_iterate_end;
    temp[game.num_impr_types] = '\0';
    secfile_insert_str(file, temp, "game.destroyed_wonders");

    calc_unit_ordering();

    players_iterate(pplayer) {
      player_save(pplayer, pplayer->player_no, file);
    } players_iterate_end;
  }
}
