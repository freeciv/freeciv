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

/*
  This file includes the definition of a new savegame format introduced with
  2.3.0. It is defined by the mandatory option '+version2'. The main load
  function checks if this option is present. If not, the old (pre-2.3.0)
  loading routines are used.
  The format version is also saved in the settings section of the savefile, as an
  integer (savefile.version). The integer is used to determine the version
  of the savefile.
  
  For each savefile format after 2.3.0, compatibility functions are defined
  which translate secfile structures from previous version to that version;
  all necessary compat functions are called in order to
  translate between the file and current version. See sg_load_compat().
 
  The integer version ID should be increased every time the format is changed.
  If the change is not backwards compatible, please state the changes in the
  following list and update the compat functions at the end of this file.

  - what was added / removed
  - when was it added / removed (date and version)
  - when can additional capability checks be set to mandatory (version)
  - which compatibility checks are needed and till when (version)

  freeciv | what                                           | date       | id
  --------+------------------------------------------------+------------+----
  current | (mapped to current savegame format)            | ----/--/-- |  0
          | first version (svn17538)                       | 2010/07/05 |  -
  2.3.0   | 2.3.0 release                                  | 2010/11/?? |  3
  2.4.0   | 2.4.0 release                                  | 201./../.. | 10
          | * player ai type                               |            |
          | * delegation                                   |            |
          | * citizens                                     |            |
          | * save player color                            |            |
          | * "known" info format change                   |            |
  2.5.0   | 2.5.0 release (development)                    | 201./../.. | 20
          |                                                |            |

  Structure of this file:

  - The main functions are savegame2_load() and savegame2_save(). Within
    former function the savegame version is tested and the requested savegame version is
    loaded.

  - The real work is done by savegame2_load_real() and savegame2_save_real().
    This function call all submodules (settings, players, etc.)

  - The remaining part of this file is split into several sections:
     * helper functions
     * save / load functions for all submodules (and their subsubmodules)

  - If possible, all functions for load / save submodules should exit in
    pairs named sg_load_<submodule> and sg_save_<submodule>. If one is not
    needed please add a comment why.

  - The submodules can be further divided as:
    sg_load_<submodule>_<subsubmodule>

  - If needed (due to static variables in the *.c files) these functions
    can be located in the corresponding source files (as done for the settings
    and the event_cache).

  Creating a savegame:

  (nothing at the moment)

  Loading a savegame:

  - The status of the process is saved within the static variable
    'sg_success'. This variable is set to TRUE within savegame2_load_real().
    If you encounter an error use sg_failure_*() to set it to FALSE and
    return an error message. Furthermore, sg_check_* should be used at the
    start of each (submodule) function to return if previous functions failed.

  - While the loading process dependencies between different modules exits.
    They can be handled within the struct loaddata *loading which is used as
    first argument for all sg_load_*() function. Please indicate the
    dependencies within the definition of this struct.

*/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* utility */
#include "bitvector.h"
#include "fcintl.h"
#include "idex.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "registry.h"
#include "shared.h"
#include "support.h"            /* bool type */
#include "timing.h"

/* common */
#include "ai.h"
#include "bitvector.h"
#include "capability.h"
#include "citizens.h"
#include "city.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "mapimg.h"
#include "movement.h"
#include "packets.h"
#include "research.h"
#include "rgbcolor.h"
#include "specialist.h"
#include "unit.h"
#include "unitlist.h"

/* server */
#include "aiiface.h"
#include "barbarian.h"
#include "citizenshand.h"
#include "citytools.h"
#include "cityturn.h"
#include "diplhand.h"
#include "maphand.h"
#include "meta.h"
#include "notify.h"
#include "plrhand.h"
#include "ruleset.h"
#include "sanitycheck.h"
#include "savegame.h"
#include "score.h"
#include "settings.h"
#include "spacerace.h"
#include "srv_main.h"
#include "stdinhand.h"
#include "techtools.h"
#include "unittools.h"

/* server/advisors */
#include "advdata.h"
#include "advbuilding.h"
#include "infracache.h"

/* server/generator */
#include "mapgen.h"
#include "utilities.h"

/* server/scripting */
#include "script_server.h"

#include "savegame2.h"

#define log_sg log_error

static bool sg_success;

#define sg_check_ret(...)                                                   \
  if (!sg_success) {                                                        \
    return;                                                                 \
  }
#define sg_check_ret_val(_val)                                              \
  if (!sg_success) {                                                        \
    return _val;                                                            \
  }

#define sg_warn(condition, message, ...)                                    \
  if (!(condition)) {                                                       \
    log_sg(message, ## __VA_ARGS__);                                        \
  }
#define sg_warn_ret(condition, message, ...)                                \
  if (!(condition)) {                                                       \
    log_sg(message, ## __VA_ARGS__);                                        \
    return;                                                                 \
  }
#define sg_warn_ret_val(condition, _val, message, ...)                      \
  if (!(condition)) {                                                       \
    log_sg(message, ## __VA_ARGS__);                                        \
    return _val;                                                            \
  }

#define sg_failure_ret(condition, message, ...)                             \
  if (!(condition)) {                                                       \
    sg_success = FALSE;                                                     \
    log_sg(message, ## __VA_ARGS__);                                        \
    sg_check_ret();                                                         \
  }
#define sg_failure_ret_val(condition, _val, message, ...)                   \
  if (!(condition)) {                                                       \
    sg_success = FALSE;                                                     \
    log_sg(message, ## __VA_ARGS__);                                        \
    sg_check_ret_val(_val);                                                 \
  }

/*
 * This loops over the entire map to save data. It collects all the data of
 * a line using GET_XY_CHAR and then executes the macro SECFILE_INSERT_LINE.
 *
 * Parameters:
 *   ptile:         current tile within the line (used by GET_XY_CHAR)
 *   GET_XY_CHAR:   macro returning the map character for each position
 *   secfile:       a secfile struct
 *   secpath, ...:  path as used for sprintf() with arguments; the last item
 *                  will be the the y coordinate
 * Example:
 *   SAVE_MAP_CHAR(ptile, terrain2char(ptile->terrain), file, "map.t%04d");
 */
#define SAVE_MAP_CHAR(ptile, GET_XY_CHAR, secfile, secpath, ...)            \
{                                                                           \
  char _line[map.xsize + 1];                                                \
  int _nat_x, _nat_y;                                                       \
                                                                            \
  for (_nat_y = 0; _nat_y < map.ysize; _nat_y++) {                          \
    for (_nat_x = 0; _nat_x < map.xsize; _nat_x++) {                        \
      struct tile *ptile = native_pos_to_tile(_nat_x, _nat_y);              \
      fc_assert_action(ptile != NULL, continue);                            \
      _line[_nat_x] = (GET_XY_CHAR);                                        \
      sg_failure_ret(fc_isprint(_line[_nat_x] & 0x7f),                      \
                     "Trying to write invalid map data at position "        \
                     "(%d, %d) for path %s: '%c' (%d)", _nat_x, _nat_y,     \
                     secpath, _line[_nat_x], _line[_nat_x]);                \
    }                                                                       \
    _line[map.xsize] = '\0';                                                \
    secfile_insert_str(secfile, _line, secpath, ## __VA_ARGS__, _nat_y);    \
  }                                                                         \
}

/*
 * This loops over the entire map to load data. It inputs a line of data
 * using the macro SECFILE_LOOKUP_LINE and then loops using the macro
 * SET_XY_CHAR to load each char into the map at (map_x, map_y). Internal
 * variables ch, map_x, map_y, nat_x, and nat_y are allocated within the
 * macro but definable by the caller.
 *
 * Parameters:
 *   ch:            a variable to hold a char (data for a single position,
 *                  used by SET_XY_CHAR)
 *   ptile:         current tile within the line (used by SET_XY_CHAR)
 *   SET_XY_CHAR:   macro to load the map character at each (map_x, map_y)
 *   secfile:       a secfile struct
 *   secpath, ...:  path as used for sprintf() with arguments; the last item
 *                  will be the the y coordinate
 * Example:
 *   LOAD_MAP_CHAR(ch, ptile,
 *                 map_get_player_tile(ptile, plr)->terrain
 *                   = char2terrain(ch), file, "player%d.map_t%04d", plrno);
 *
 * Note: some (but not all) of the code this is replacing used to skip over
 *       lines that did not exist. This allowed for backward-compatibility.
 *       We could add another parameter that specified whether it was OK to
 *       skip the data, but there's not really much advantage to exiting
 *       early in this case. Instead, we let any map data type to be empty,
 *       and just print an informative warning message about it.
 */
#define LOAD_MAP_CHAR(ch, ptile, SET_XY_CHAR, secfile, secpath, ...)        \
{                                                                           \
  int _nat_x, _nat_y;                                                       \
  bool _printed_warning = FALSE;                                            \
  for (_nat_y = 0; _nat_y < map.ysize; _nat_y++) {                          \
    const char *_line = secfile_lookup_str(secfile, secpath,                \
                                           ## __VA_ARGS__, _nat_y);         \
    if (NULL == _line) {                                                    \
      char buf[64];                                                         \
      fc_snprintf(buf, sizeof(buf), secpath, ## __VA_ARGS__, _nat_y);       \
      log_verbose("Line not found='%s'", buf);                              \
      _printed_warning = TRUE;                                              \
      continue;                                                             \
    } else if (strlen(_line) != map.xsize) {                                \
      char buf[64];                                                         \
      fc_snprintf(buf, sizeof(buf), secpath, ## __VA_ARGS__, _nat_y);       \
      log_verbose("Line too short (expected %d got %lu)='%s'",              \
                  map.xsize, (unsigned long) strlen(_line), buf);           \
      _printed_warning = TRUE;                                              \
      continue;                                                             \
    }                                                                       \
    for (_nat_x = 0; _nat_x < map.xsize; _nat_x++) {                        \
      const char ch = _line[_nat_x];                                        \
      struct tile *ptile = native_pos_to_tile(_nat_x, _nat_y);              \
      (SET_XY_CHAR);                                                        \
    }                                                                       \
  }                                                                         \
  if (_printed_warning) {                                                   \
    /* TRANS: Minor error message. */                                       \
    log_sg(_("Saved game contains incomplete map data. This can"            \
             " happen with old saved games, or it may indicate an"          \
             " invalid saved game file. Proceed at your own risk."));       \
  }                                                                         \
}

/* Iterate on the specials half-bytes */
#define halfbyte_iterate_special(s, num_specials_types)                     \
{                                                                           \
  enum tile_special_type s;                                                 \
  for(s = 0; 4 * s < (num_specials_types); s++) {

#define halfbyte_iterate_special_end                                        \
  }                                                                         \
}

/* Iterate on the bases half-bytes */
#define halfbyte_iterate_bases(b, num_bases_types)                          \
{                                                                           \
  int b;                                                                    \
  for(b = 0; 4 * b < (num_bases_types); b++) {

#define halfbyte_iterate_bases_end                                          \
  }                                                                         \
}

/* Iterate on the roads half-bytes */
#define halfbyte_iterate_roads(r, num_roads_types)                          \
{                                                                           \
  int r;                                                                    \
  for(r = 0; 4 * r < (num_roads_types); r++) {

#define halfbyte_iterate_roads_end                                          \
  }                                                                         \
}

struct loaddata {
  struct section_file *file;
  const char *secfile_options;

  /* loaded in sg_load_savefile(); needed in sg_load_player() */
  struct {
    const char **order;
    size_t size;
  } improvement;
  /* loaded in sg_load_savefile(); needed in sg_load_player() */
  struct {
    const char **order;
    size_t size;
  } technology;
  /* loaded in sg_load_savefile(); needed in sg_load_map(), ... */
  struct {
    enum tile_special_type *order;
    size_t size;
  } special;
  /* loaded in sg_load_savefile(); needed in sg_load_map(), ... */
  struct {
    struct base_type **order;
    size_t size;
  } base;
  /* loaded in sg_load_savefile(); needed in sg_load_map(), ... */
  struct {
    struct road_type **order;
    size_t size;
  } road;

  /* loaded in sg_load_game(); needed in sg_load_random(), ... */
  enum server_states server_state;

  /* loaded in sg_load_random(); needed in sg_load_sanitycheck() */
  RANDOM_STATE rstate;

  /* loaded in sg_load_map_worked(); needed in sg_load_player_cities() */
  int *worked_tiles;
};

struct savedata {
  struct section_file *file;
  char secfile_options[512];

  /* set by the caller */
  const char *save_reason;
  bool scenario;

  /* Set in sg_save_game(); needed in sg_save_map_*(); ... */
  bool save_players;
};

#define TOKEN_SIZE 10

#define log_worker      log_verbose

static const char savefile_options_default[] =
  " +version2";
/* The following savefile option are added if needed:
 *  - specials
 *  - riversoverlay
 * See also calls to sg_save_savefile_options(). */

static const char hex_chars[] = "0123456789abcdef";
static const char num_chars[] =
  "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-+";

static void savegame2_load_real(struct section_file *file);
static void savegame2_save_real(struct section_file *file,
                                const char *save_reason,
                                bool scenario);
struct loaddata *loaddata_new(struct section_file *file);
void loaddata_destroy(struct loaddata *loading);

struct savedata *savedata_new(struct section_file *file,
                              const char *save_reason,
                              bool scenario);
void savedata_destroy(struct savedata *saving);

static enum unit_orders char2order(char order);
static char order2char(enum unit_orders order);
static enum direction8 char2dir(char dir);
static char dir2char(enum direction8 dir);
static char activity2char(enum unit_activity activity);
static enum unit_activity char2activity(char activity);
static char *quote_block(const void *const data, int length);
static int unquote_block(const char *const quoted_, void *dest,
                         int dest_length);
static void worklist_load(struct section_file *file, struct worklist *pwl,
                          const char *path, ...);
static void worklist_save(struct section_file *file,
                          const struct worklist *pwl,
                          int max_length, const char *path, ...);
static void unit_ordering_calc(void);
static void unit_ordering_apply(void);
static void sg_special_set(bv_special *specials, char ch,
                           const enum tile_special_type *index,
                           bool rivers_overlay);
static char sg_special_get(bv_special specials,
                           const enum tile_special_type *index);
static void sg_bases_set(bv_bases *bases, char ch, struct base_type **index);
static char sg_bases_get(bv_bases bases, const int *index);
static void sg_roads_set(bv_roads *roads, char ch, struct road_type **index);
static char sg_roads_get(bv_roads roads, const int *index);
static struct resource *char2resource(char c);
static char resource2char(const struct resource *presource);
/* bin2ascii_hex() is defined as macro */
static int ascii_hex2bin(char ch, int halfbyte);
static char num2char(unsigned int num);
static int char2num(char ch);
static struct terrain *char2terrain(char ch);
static char terrain2char(const struct terrain *pterrain);
static Tech_type_id technology_load(struct section_file *file,
                                    const char* path, int plrno);
static void technology_save(struct section_file *file,
                            const char* path, int plrno, Tech_type_id tech);

static void sg_load_compat(struct loaddata *loading);

static void sg_load_savefile(struct loaddata *loading);
static void sg_save_savefile(struct savedata *saving);
static void sg_save_savefile_options(struct savedata *saving,
                                     const char *option);

static void sg_load_game(struct loaddata *loading);
static void sg_save_game(struct savedata *saving);

static void sg_load_random(struct loaddata *loading);
static void sg_save_random(struct savedata *saving);

static void sg_load_script(struct loaddata *loading);
static void sg_save_script(struct savedata *saving);

static void sg_load_scenario(struct loaddata *loading);
static void sg_save_scenario(struct savedata *saving);

static void sg_load_settings(struct loaddata *loading);
static void sg_save_settings(struct savedata *saving);

static void sg_load_map(struct loaddata *loading);
static void sg_save_map(struct savedata *saving);
static void sg_load_map_tiles(struct loaddata *loading);
static void sg_save_map_tiles(struct savedata *saving);
static void sg_load_map_tiles_bases(struct loaddata *loading);
static void sg_save_map_tiles_bases(struct savedata *saving);
static void sg_load_map_tiles_roads(struct loaddata *loading);
static void sg_save_map_tiles_roads(struct savedata *saving);
static void sg_load_map_tiles_specials(struct loaddata *loading,
                                       bool rivers_overlay);
static void sg_save_map_tiles_specials(struct savedata *saving,
                                       bool rivers_overlay);
static void sg_load_map_tiles_resources(struct loaddata *loading);
static void sg_save_map_tiles_resources(struct savedata *saving);

static void sg_load_map_startpos(struct loaddata *loading);
static void sg_save_map_startpos(struct savedata *saving);
static void sg_load_map_owner(struct loaddata *loading);
static void sg_save_map_owner(struct savedata *saving);
static void sg_load_map_worked(struct loaddata *loading);
static void sg_save_map_worked(struct savedata *saving);
static void sg_load_map_known(struct loaddata *loading);
static void sg_save_map_known(struct savedata *saving);

static void sg_load_players_basic(struct loaddata *loading);
static void sg_load_players(struct loaddata *loading);
static void sg_load_player_main(struct loaddata *loading,
                                struct player *plr);
static void sg_load_player_cities(struct loaddata *loading,
                                  struct player *plr);
static bool sg_load_player_city(struct loaddata *loading, struct player *plr,
                                struct city *pcity, const char *citystr);
static void sg_load_player_city_citizens(struct loaddata *loading,
                                         struct player *plr,
                                         struct city *pcity,
                                         const char *citystr);
static void sg_load_player_units(struct loaddata *loading,
                                 struct player *plr);
static bool sg_load_player_unit(struct loaddata *loading,
                                struct player *plr, struct unit *punit,
                                const char *unitstr);
static void sg_load_player_units_transport(struct loaddata *loading,
                                           struct player *plr);
static void sg_load_player_attributes(struct loaddata *loading,
                                      struct player *plr);
static void sg_load_player_vision(struct loaddata *loading,
                                  struct player *plr);
static bool sg_load_player_vision_city(struct loaddata *loading,
                                       struct player *plr,
                                       struct vision_site *pdcity,
                                       const char *citystr);
static void sg_save_players(struct savedata *saving);
static void sg_save_player_main(struct savedata *saving,
                                struct player *plr);
static void sg_save_player_cities(struct savedata *saving,
                                  struct player *plr);
static void sg_save_player_units(struct savedata *saving,
                                 struct player *plr);
static void sg_save_player_attributes(struct savedata *saving,
                                      struct player *plr);
static void sg_save_player_vision(struct savedata *saving,
                                  struct player *plr);

static void sg_load_event_cache(struct loaddata *loading);
static void sg_save_event_cache(struct savedata *saving);

static void sg_load_mapimg(struct loaddata *loading);
static void sg_save_mapimg(struct savedata *saving);

static void sg_load_sanitycheck(struct loaddata *loading);
static void sg_save_sanitycheck(struct savedata *saving);



typedef void (*load_version_func_t) (struct loaddata *loading);

static void compat_load_020400(struct loaddata *loading);
static void compat_load_020500(struct loaddata *loading);

struct compatibility {
  int version;
  const struct sset_val_name name;
  const load_version_func_t load;
};

/* The struct below contains the information about the savegame versions. It
 * is identified by the version number (first element), which should be
 * steadily increasing. It is saved as 'savefile.version'. The support
 * string (first element of 'name') is not saved in the savegame; it is
 * saved in settings files (so, once assigned, cannot be changed). The
 * 'pretty' string (second element of 'name') can be changed if necessary
 * For changes in the development version, edit the definitions above and
 * add the needed code to load the old version below. Thus, old
 * savegames can still be loaded while the main definition
 * represents the current state of the art. */
/* While developing freeciv 2.5.0, add the compatibility functions to
 * - compat_load_020500 to load old savegame. */
static struct compatibility compat[] = {
  /* dummy; equal to the current version (last element) */
  { 0, { "CURRENT", N_("current version") }, NULL },
  /* version 1 and 2 is not used */
  /* version 3: first savegame2 format, so no compat functions for translation
   * from previous format */
  { 3, { "2.3.0", N_("freeciv 2.3.0") }, NULL },
  /* version 4 to 9 are reserved for possible changes in 2.3.x */
  { 10, { "2.4.0", N_("freeciv 2.4.0") },
    compat_load_020400 },
  /* version 11 to 19 are reserved for possible changes in 2.4.x */
  { 20, { "2.5.0", N_("freeciv 2.5.0 (development)") },
    compat_load_020500 },
  /* Current savefile version is listed above this line; it corresponds to
     the definitions in this file. */
};

static const int compat_num = ARRAY_SIZE(compat);
#define compat_current (compat_num - 1)

/****************************************************************************
  Main entry point for loading a game.
  Called only in ./server/stdinhand.c:load_command().
  The entire ruleset is always sent afterwards->
****************************************************************************/
void savegame2_load(struct section_file *file)
{
  const char *savefile_options;

  fc_assert_ret(file != NULL);

#ifdef DEBUG_TIMERS
  struct timer *loadtimer = new_timer_start(TIMER_CPU, TIMER_DEBUG);
#endif

  savefile_options = secfile_lookup_str(file, "savefile.options");

  if (!savefile_options) {
    log_error("Missing savefile options. Can not load the savegame.");
    return;
  }

  if (!has_capabilities("+version2", savefile_options)) {
    /* load old format (freeciv 2.2.x) */
    log_verbose("loading savefile in old format ...");
    legacy_game_load(file);
  } else {
    /* load new format (freeciv 2.2.99 and newer) */
    log_verbose("loading savefile in new format ...");
    savegame2_load_real(file);
  }

#ifdef DEBUG_TIMERS
  stop_timer(loadtimer);
  log_debug("Loading secfile in %.3f seconds.", read_timer_seconds(loadtimer));
  free_timer(loadtimer);
#endif /* DEBUG_TIMERS */
}

/****************************************************************************
  Main entry point for saving a game.
  Called only in ./server/srv_main.c:save_game().
****************************************************************************/
void savegame2_save(struct section_file *file, const char *save_reason,
                    bool scenario)
{
  fc_assert_ret(file != NULL);

#ifdef DEBUG_TIMERS
  struct timer *savetimer = new_timer_start(TIMER_CPU, TIMER_DEBUG);
#endif

  log_verbose("saving game in new format ...");
  savegame2_save_real(file, save_reason, scenario);

#ifdef DEBUG_TIMERS
  stop_timer(savetimer);
  log_debug("Creating secfile in %.3f seconds.", read_timer_seconds(savetimer));
  free_timer(savetimer);
#endif /* DEBUG_TIMERS */
}

/* =======================================================================
 * Basic load / save functions.
 * ======================================================================= */

/****************************************************************************
  Really loading the savegame.
****************************************************************************/
static void savegame2_load_real(struct section_file *file)
{
  struct loaddata *loading;
  bool was_send_city_suppressed, was_send_tile_suppressed;

  /* initialise loading */
  was_send_city_suppressed = send_city_suppression(TRUE);
  was_send_tile_suppressed = send_tile_suppression(TRUE);
  loading = loaddata_new(file);
  sg_success = TRUE;

  /* Load the savegame data. */
  /* [compat] */
  sg_load_compat(loading);
  /* [savefile] */
  sg_load_savefile(loading);
  /* [game] */
  sg_load_game(loading);
  /* [random] */
  sg_load_random(loading);
  /* [script] */
  sg_load_script(loading);
  /* [scenario] */
  sg_load_scenario(loading);
  /* [settings] */
  sg_load_settings(loading);
  /* [players] (basic data) */
  sg_load_players_basic(loading);
  /* [map]; needs width and height loaded by [settings]  */
  sg_load_map(loading);
  /* [player<i>] */
  sg_load_players(loading);
  /* [event_cache] */
  sg_load_event_cache(loading);
  /* [mapimg] */
  sg_load_mapimg(loading);

  /* Sanity checks for the loaded game. */
  sg_load_sanitycheck(loading);

  /* deinitialise loading */
  loaddata_destroy(loading);
  send_tile_suppression(was_send_tile_suppressed);
  send_city_suppression(was_send_city_suppressed);

  if (!sg_success) {
    log_error("Failure loading savegame!");
    game_reset();
  }
}

/****************************************************************************
  Really save the game to a file.
****************************************************************************/
static void savegame2_save_real(struct section_file *file,
                                const char *save_reason,
                                bool scenario)
{
  struct savedata *saving;

  /* initialise loading */
  saving = savedata_new(file, save_reason, scenario);
  sg_success = TRUE;

  /* [savefile] */
  sg_save_savefile(saving);
  /* [game] */
  sg_save_game(saving);
  /* [random] */
  sg_save_random(saving);
  /* [script] */
  sg_save_script(saving);
  /* [scenario] */
  sg_save_scenario(saving);
  /* [settings] */
  sg_save_settings(saving);
  /* [map] */
  sg_save_map(saving);
  /* [player<i>] */
  sg_save_players(saving);
  /* [event_cache] */
  sg_save_event_cache(saving);
  /* [mapimg] */
  sg_save_mapimg(saving);

  /* Sanity checks for the saved game. */
  sg_save_sanitycheck(saving);

  /* deinitialise saving */
  savedata_destroy(saving);

  if (!sg_success) {
    log_error("Failure saving savegame!");
  }
}

/****************************************************************************
  Create new loaddata item for given section file.
****************************************************************************/
struct loaddata *loaddata_new(struct section_file *file)
{
  struct loaddata *loading = calloc(1, sizeof(*loading));
  loading->file = file;
  loading->secfile_options = NULL;

  loading->improvement.order = NULL;
  loading->improvement.size = -1;
  loading->technology.order = NULL;
  loading->technology.size = -1;
  loading->special.order = NULL;
  loading->special.size = -1;
  loading->base.order = NULL;
  loading->base.size = -1;
  loading->road.order = NULL;
  loading->road.size = -1;

  loading->server_state = S_S_INITIAL;
  loading->rstate = fc_rand_state();
  loading->worked_tiles = NULL;

  return loading;
}

/****************************************************************************
  Free resources allocated for loaddata item.
****************************************************************************/
void loaddata_destroy(struct loaddata *loading)
{
  if (loading->improvement.order != NULL) {
    free(loading->improvement.order);
  }

  if (loading->technology.order != NULL) {
    free(loading->technology.order);
  }

  if (loading->special.order != NULL) {
    free(loading->special.order);
  }

  if (loading->base.order != NULL) {
    free(loading->base.order);
  }

  if (loading->road.order != NULL) {
    free(loading->road.order);
  }

  if (loading->worked_tiles != NULL) {
    free(loading->worked_tiles);
  }

  free(loading);
}

/****************************************************************************
  Create new savedata item for given file.
****************************************************************************/
struct savedata *savedata_new(struct section_file *file,
                              const char *save_reason,
                              bool scenario)
{
  struct savedata *saving = calloc(1, sizeof(*saving));
  saving->file = file;
  saving->secfile_options[0] = '\0';

  saving->save_reason = save_reason;
  saving->scenario = scenario;

  saving->save_players = FALSE;

  return saving;
}

/****************************************************************************
  Free resources allocated for savedata item
****************************************************************************/
void savedata_destroy(struct savedata *saving)
{
  free(saving);
}

/* =======================================================================
 * Helper functions.
 * ======================================================================= */

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
    return ORDER_TRADE_ROUTE;
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
  case ORDER_TRADE_ROUTE:
    return 't';
  case ORDER_HOMECITY:
    return 'h';
  case ORDER_LAST:
    break;
  }

  fc_assert(FALSE);
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
  return direction8_invalid();
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

  fc_assert(FALSE);
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
  case ACTIVITY_GEN_ROAD:
    return 'R';
  case ACTIVITY_CONVERT:
    return 'c';
  case ACTIVITY_UNKNOWN:
  case ACTIVITY_PATROL_UNUSED:
    return '?';
  case ACTIVITY_LAST:
    break;
  }

  fc_assert(FALSE);
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

    if (activity == achar || activity == fc_toupper(achar)) {
      return a;
    }
  }

  /* This can happen if the savegame is invalid. */
  return ACTIVITY_LAST;
}

/****************************************************************************
  Quote the memory block denoted by data and length so it consists only of
  " a-f0-9:". The returned string has to be freed by the caller using free().
****************************************************************************/
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

/****************************************************************************
  Unquote a string. The unquoted data is written into dest. If the unqoted
  data will be largern than dest_length the function aborts. It returns the
  actual length of the unquoted block.
****************************************************************************/
static int unquote_block(const char *const quoted_, void *dest,
                         int dest_length)
{
  int i, length, parsed, tmp;
  char *endptr;
  const char *quoted = quoted_;

  parsed = sscanf(quoted, "%d", &length);
  fc_assert_ret_val(1 == parsed, 0);

  fc_assert_ret_val(length <= dest_length, 0);
  quoted = strchr(quoted, ':');
  fc_assert_ret_val(quoted != NULL, 0);
  quoted++;

  for (i = 0; i < length; i++) {
    tmp = strtol(quoted, &endptr, 16);
    fc_assert_ret_val((endptr - quoted) == 2, 0);
    fc_assert_ret_val(*endptr == ' ', 0);
    fc_assert_ret_val((tmp & 0xff) == tmp, 0);
    ((unsigned char *) dest)[i] = tmp;
    quoted += 3;
  }
  return length;
}

/****************************************************************************
  Load the worklist elements specified by path to the worklist pointed to
  by 'pwl'. 'pwl' should be a pointer to an existing worklist.
****************************************************************************/
static void worklist_load(struct section_file *file, struct worklist *pwl,
                          const char *path, ...)
{
  int i;
  const char *kind;
  const char *name;
  char path_str[1024];
  va_list ap;

  /* The first part of the registry path is taken from the varargs to the
   * function. */
  va_start(ap, path);
  fc_vsnprintf(path_str, sizeof(path_str), path, ap);
  va_end(ap);

  worklist_init(pwl);
  pwl->length = secfile_lookup_int_default(file, 0,
                                           "%s.wl_length", path_str);

  for (i = 0; i < pwl->length; i++) {
    kind = secfile_lookup_str(file, "%s.wl_kind%d", path_str, i);

    /* We lookup the production value by name. An invalid entry isn't a
     * fatal error; we just truncate the worklist. */
    name = secfile_lookup_str_default(file, "-", "%s.wl_value%d",
                                      path_str, i);
    pwl->entries[i] = universal_by_rule_name(kind, name);
    if (pwl->entries[i].kind == universals_n_invalid()) {
      log_sg("%s.wl_value%d: unknown \"%s\" \"%s\".", path_str, i, kind,
             name);
      pwl->length = i;
      break;
    }
  }
}

/****************************************************************************
  Save the worklist elements specified by path from the worklist pointed to
  by 'pwl'. 'pwl' should be a pointer to an existing worklist.
****************************************************************************/
static void worklist_save(struct section_file *file,
                          const struct worklist *pwl,
                          int max_length, const char *path, ...)
{
  char path_str[1024];
  int i;
  va_list ap;

  /* The first part of the registry path is taken from the varargs to the
   * function. */
  va_start(ap, path);
  fc_vsnprintf(path_str, sizeof(path_str), path, ap);
  va_end(ap);

  secfile_insert_int(file, pwl->length, "%s.wl_length", path_str);

  for (i = 0; i < pwl->length; i++) {
    const struct universal *entry = pwl->entries + i;
    secfile_insert_str(file, universal_type_rule_name(entry),
                       "%s.wl_kind%d", path_str, i);
    secfile_insert_str(file, universal_rule_name(entry),
                       "%s.wl_value%d", path_str, i);
  }

  fc_assert_ret(max_length <= MAX_LEN_WORKLIST);

  /* We want to keep savegame in tabular format, so each line has to be
   * of equal length. Fill table up to maximum worklist size. */
  for (i = pwl->length ; i < max_length; i++) {
    secfile_insert_str(file, "", "%s.wl_kind%d", path_str, i);
    secfile_insert_str(file, "", "%s.wl_value%d", path_str, i);
  }
}

/****************************************************************************
  Assign values to ord_city and ord_map for each unit, so the values can be
  saved.
****************************************************************************/
static void unit_ordering_calc(void)
{
  int j;

  players_iterate(pplayer) {
    /* to avoid junk values for unsupported units: */
    unit_list_iterate(pplayer->units, punit) {
      punit->server.ord_city = 0;
    } unit_list_iterate_end;
    city_list_iterate(pplayer->cities, pcity) {
      j = 0;
      unit_list_iterate(pcity->units_supported, punit) {
        punit->server.ord_city = j++;
      } unit_list_iterate_end;
    } city_list_iterate_end;
  } players_iterate_end;

  whole_map_iterate(ptile) {
    j = 0;
    unit_list_iterate(ptile->units, punit) {
      punit->server.ord_map = j++;
    } unit_list_iterate_end;
  } whole_map_iterate_end;
}

/****************************************************************************
  For each city and tile, sort unit lists according to ord_city and ord_map
  values.
****************************************************************************/
static void unit_ordering_apply(void)
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

/****************************************************************************
  Complicated helper function for loading specials from a savegame.

  'ch' gives the character loaded from the savegame. Specials are packed
  in four to a character in hex notation. 'index' is a mapping of
  savegame bit -> special bit. S_LAST is used to mark unused savegame bits.
****************************************************************************/
static void sg_special_set(bv_special *specials, char ch,
                           const enum tile_special_type *index,
                           bool rivers_overlay)
{
  int i, bin;
  const char *pch = strchr(hex_chars, ch);

  if (!pch || ch == '\0') {
    log_sg("Unknown hex value: '%c' (%d)", ch, ch);
    bin = 0;
  } else {
    bin = pch - hex_chars;
  }

  for (i = 0; i < 4; i++) {
    enum tile_special_type sp = index[i];

    if (sp >= S_LAST) {
      continue;
    }
    if (rivers_overlay && sp != S_RIVER) {
      continue;
    }

    if (bin & (1 << i)) {
      set_special(specials, sp);
    }
  }
}

/****************************************************************************
  Complicated helper function for saving specials into a savegame.

  Specials are packed in four to a character in hex notation. 'index'
  specifies which set of specials are included in this character.
****************************************************************************/
static char sg_special_get(bv_special specials,
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
  Helper function for loading bases from a savegame.

  'ch' gives the character loaded from the savegame. Bases are packed
  in four to a character in hex notation. 'index' is a mapping of
  savegame bit -> base bit.
****************************************************************************/
static void sg_bases_set(bv_bases *bases, char ch, struct base_type **index)
{
  int i, bin;
  const char *pch = strchr(hex_chars, ch);

  if (!pch || ch == '\0') {
    log_sg("Unknown hex value: '%c' (%d)", ch, ch);
    bin = 0;
  } else {
    bin = pch - hex_chars;
  }

  for (i = 0; i < 4; i++) {
    struct base_type *pbase = index[i];

    if (pbase == NULL) {
      continue;
    }
    if (bin & (1 << i)) {
      BV_SET(*bases, base_index(pbase));
    }
  }
}

/****************************************************************************
  Helper function for saving bases into a savegame.

  Specials are packed in four to a character in hex notation. 'index'
  specifies which set of bases are included in this character.
****************************************************************************/
static char sg_bases_get(bv_bases bases, const int *index)
{
  int i, bin = 0;

  for (i = 0; i < 4; i++) {
    int base = index[i];

    if (base < 0) {
      break;
    }
    if (BV_ISSET(bases, base)) {
      bin |= (1 << i);
    }
  }

  return hex_chars[bin];
}

/****************************************************************************
  Helper function for loading roads from a savegame.

  'ch' gives the character loaded from the savegame. Roads are packed
  in four to a character in hex notation. 'index' is a mapping of
  savegame bit -> road bit.
****************************************************************************/
static void sg_roads_set(bv_roads *roads, char ch, struct road_type **index)
{
  int i, bin;
  const char *pch = strchr(hex_chars, ch);

  if (!pch || ch == '\0') {
    log_sg("Unknown hex value: '%c' (%d)", ch, ch);
    bin = 0;
  } else {
    bin = pch - hex_chars;
  }

  for (i = 0; i < 4; i++) {
    struct road_type *proad = index[i];

    if (proad == NULL) {
      continue;
    }
    if (bin & (1 << i)) {
      BV_SET(*roads, road_index(proad));
    }
  }
}

/****************************************************************************
  Helper function for saving roads into a savegame.

  Specials are packed in four to a character in hex notation. 'index'
  specifies which set of roads are included in this character.
****************************************************************************/
static char sg_roads_get(bv_roads roads, const int *index)
{
  int i, bin = 0;

  for (i = 0; i < 4; i++) {
    int road = index[i];

    if (road < 0) {
      break;
    }
    if (BV_ISSET(roads, road)) {
      bin |= (1 << i);
    }
  }

  return hex_chars[bin];
}

/****************************************************************************
  Return the resource for the given identifier.
****************************************************************************/
static struct resource *char2resource(char c)
{
  /* speed common values */
  if (c == RESOURCE_NULL_IDENTIFIER
   || c == RESOURCE_NONE_IDENTIFIER) {
    return NULL;
  }
  return resource_by_identifier(c);
}

/****************************************************************************
  Return the identifier for the given resource.
****************************************************************************/
static char resource2char(const struct resource *presource)
{
  return presource ? presource->identifier : RESOURCE_NONE_IDENTIFIER;
}

/****************************************************************************
  This returns an ascii hex value of the given half-byte of the binary
  integer. See ascii_hex2bin().
  example: bin2ascii_hex(0xa00, 2) == 'a'
****************************************************************************/
#define bin2ascii_hex(value, halfbyte_wanted) \
  hex_chars[((value) >> ((halfbyte_wanted) * 4)) & 0xf]

/****************************************************************************
  This returns a binary integer value of the ascii hex char, offset by the
  given number of half-bytes. See bin2ascii_hex().
  example: ascii_hex2bin('a', 2) == 0xa00
  This is only used in loading games, and it requires some error checking so
  it's done as a function.
****************************************************************************/
static int ascii_hex2bin(char ch, int halfbyte)
{
  const char *pch;

  if (ch == ' ') {
    /* Sane value. It is unknow if there are savegames out there which
     * need this fix. Savegame.c doesn't write such savegames
     * (anymore) since the inclusion into CVS (2000-08-25). */
    return 0;
  }

  pch = strchr(hex_chars, ch);

  sg_failure_ret_val(NULL != pch && '\0' != ch, 0,
                     "Unknown hex value: '%c' %d", ch, ch);
  return (pch - hex_chars) << (halfbyte * 4);
}

/****************************************************************************
  Converts number in to single character. This works to values up to ~70.
****************************************************************************/
static char num2char(unsigned int num)
{
  if (num >= strlen(num_chars)) {
    return '?';
  }

  return num_chars[num];
}

/****************************************************************************
  Converts single character into numerical value. This is not hex conversion.
****************************************************************************/
static int char2num(char ch)
{
  const char *pch;

  pch = strchr(num_chars, ch);

  sg_failure_ret_val(NULL != pch, 0,
                     "Unknown ascii value for num: '%c' %d", ch, ch);

  return pch - num_chars;
}

/****************************************************************************
  Dereferences the terrain character.  See terrains[].identifier
    example: char2terrain('a') => T_ARCTIC
****************************************************************************/
static struct terrain *char2terrain(char ch)
{
  /* terrain_by_identifier plus fatal error */
  if (ch == TERRAIN_UNKNOWN_IDENTIFIER) {
    return T_UNKNOWN;
  }
  terrain_type_iterate(pterrain) {
    if (pterrain->identifier == ch) {
      return pterrain;
    }
  } terrain_type_iterate_end;

  log_fatal("Unknown terrain identifier '%c' in savegame.", ch);
  exit(EXIT_FAILURE);
}

/****************************************************************************
  References the terrain character.  See terrains[].identifier
    example: terrain2char(T_ARCTIC) => 'a'
****************************************************************************/
static char terrain2char(const struct terrain *pterrain)
{
  if (pterrain == T_UNKNOWN) {
    return TERRAIN_UNKNOWN_IDENTIFIER;
  } else {
    return pterrain->identifier;
  }
}

/*****************************************************************************
  Load technology from path_name and if doesn't exist (because savegame
  is too old) load from path.
*****************************************************************************/
static Tech_type_id technology_load(struct section_file *file,
                                    const char* path, int plrno)
{
  char path_with_name[128];
  const char* name;
  struct advance *padvance;

  fc_snprintf(path_with_name, sizeof(path_with_name),
              "%s_name", path);

  name = secfile_lookup_str(file, path_with_name, plrno);

  if (!name || name[0] == '\0') {
    /* used by researching_saved */
    return A_UNKNOWN;
  }
  if (fc_strcasecmp(name, "A_FUTURE") == 0) {
    return A_FUTURE;
  }
  if (fc_strcasecmp(name, "A_NONE") == 0) {
    return A_NONE;
  }
  if (fc_strcasecmp(name, "A_UNSET") == 0) {
    return A_UNSET;
  }

  padvance = advance_by_rule_name(name);
  sg_failure_ret_val(NULL != padvance, A_NONE,
                     "%s: unknown technology \"%s\".", path_with_name, name);

  return advance_number(padvance);
}

/*****************************************************************************
  Save technology in secfile entry called path_name.
*****************************************************************************/
static void technology_save(struct section_file *file,
                            const char* path, int plrno, Tech_type_id tech)
{
  char path_with_name[128];
  const char* name;

  fc_snprintf(path_with_name, sizeof(path_with_name),
              "%s_name", path);

  switch (tech) {
    case A_UNKNOWN: /* used by researching_saved */
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
      name = advance_rule_name(advance_by_number(tech));
      break;
  }

  secfile_insert_str(file, name, path_with_name, plrno);
}

/* =======================================================================
 * Load / save savefile data.
 * ======================================================================= */

/****************************************************************************
  Load '[savefile]'.
****************************************************************************/
static void sg_load_savefile(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* Load savefile options. */
  loading->secfile_options
    = secfile_lookup_str(loading->file, "savefile.options");

  /* We don't need savefile.reason, but read it anyway to avoid
   * warnings about unread secfile entries. */
  (void) secfile_lookup_str_default(loading->file, "None",
                                    "savefile.reason");

  /* Load ruleset. */
  sz_strlcpy(game.server.rulesetdir,
             secfile_lookup_str_default(loading->file, "classic",
                                        "savefile.rulesetdir"));
  if (!strcmp("default", game.server.rulesetdir)) {
    sz_strlcpy(game.server.rulesetdir, "classic");
  }
  load_rulesets();

  /* Load improvements. */
  loading->improvement.size
    = secfile_lookup_int_default(loading->file, 0,
                                 "savefile.improvement_size");
  if (loading->improvement.size) {
    loading->improvement.order
      = secfile_lookup_str_vec(loading->file, &loading->improvement.size,
                               "savefile.improvement_vector");
    sg_failure_ret(loading->improvement.size != 0,
                   "Failed to load improvement order: %s",
                   secfile_error());
  }

  /* Load technologies. */
  loading->technology.size
    = secfile_lookup_int_default(loading->file, 0,
                                 "savefile.technology_size");
  if (loading->technology.size) {
    loading->technology.order
      = secfile_lookup_str_vec(loading->file, &loading->technology.size,
                               "savefile.technology_vector");
    sg_failure_ret(loading->improvement.size != 0,
                   "Failed to load technology order: %s",
                   secfile_error());
  }

  /* Load specials. */
  loading->special.size
    = secfile_lookup_int_default(loading->file, 0,
                                 "savefile.special_size");
  {
    const char **modname;
    size_t nmod;
    enum tile_special_type j;

    modname = secfile_lookup_str_vec(loading->file, &loading->special.size,
                                     "savefile.specials_vector");
    sg_failure_ret(loading->special.size != 0,
                   "Failed to load specials order: %s",
                   secfile_error());
    /* make sure that the size of the array is divisible by 4 */
    /* Allocating extra 4 slots, just a couple of bytes,
     * in case of special.size being divisible by 4 already is intentional.
     * Added complexity would cost those couple of bytes in code size alone,
     * and we actually need at least one slot immediately after last valid
     * one. That's where S_LAST is (or was in version that saved the game)
     * and in some cases S_LAST gets written to savegame, at least as
     * activity target special when activity targets some base or road
     * instead. By having current S_LAST in that index allows us to map
     * that old S_LAST to current S_LAST, just like any real special within
     * special.size gets mapped. */
    nmod = loading->special.size + (4 - (loading->special.size % 4));
    loading->special.order = fc_calloc(nmod,
                                       sizeof(*loading->special.order));
    for (j = 0; j < loading->special.size; j++) {
      loading->special.order[j] = special_by_rule_name(modname[j]);
    }
    free(modname);
    for (; j < nmod; j++) {
      loading->special.order[j] = S_LAST;
    }
  }

  /* Load bases. */
  loading->base.size
    = secfile_lookup_int_default(loading->file, 0,
                                 "savefile.bases_size");
  if (loading->base.size) {
    const char **modname;
    size_t nmod;
    int j;

    modname = secfile_lookup_str_vec(loading->file, &loading->base.size,
                                     "savefile.bases_vector");
    sg_failure_ret(loading->base.size != 0,
                   "Failed to load bases order: %s",
                   secfile_error());
    sg_failure_ret(!(game.control.num_base_types < loading->base.size),
                   "Number of bases defined by the ruleset (= %d) are "
                   "lower than the number in the savefile (= %d).",
                   game.control.num_base_types, (int)loading->base.size);
    /* make sure that the size of the array is divisible by 4 */
    nmod = 4 * ((loading->base.size + 3) / 4);
    loading->base.order = fc_calloc(nmod, sizeof(*loading->base.order));
    for (j = 0; j < loading->base.size; j++) {
      loading->base.order[j] = base_type_by_rule_name(modname[j]);
    }
    free(modname);
    for (; j < nmod; j++) {
      loading->base.order[j] = NULL;
    }
  }

  /* Load roads. */
  loading->road.size
    = secfile_lookup_int_default(loading->file, 0,
                                 "savefile.roads_size");
  if (loading->road.size) {
    const char **modname;
    size_t nmod;
    int j;

    modname = secfile_lookup_str_vec(loading->file, &loading->road.size,
                                     "savefile.roads_vector");
    sg_failure_ret(loading->road.size != 0,
                   "Failed to load roads order: %s",
                   secfile_error());
    sg_failure_ret(!(game.control.num_road_types < loading->road.size),
                   "Number of roads defined by the ruleset (= %d) are "
                   "lower than the number in the savefile (= %d).",
                   game.control.num_road_types, (int)loading->road.size);
    /* make sure that the size of the array is divisible by 4 */
    nmod = 4 * ((loading->road.size + 3) / 4);
    loading->road.order = fc_calloc(nmod, sizeof(*loading->road.order));
    for (j = 0; j < loading->road.size; j++) {
      loading->road.order[j] = road_type_by_rule_name(modname[j]);
    }
    free(modname);
    for (; j < nmod; j++) {
      loading->road.order[j] = NULL;
    }
  }
}

/****************************************************************************
  Save '[savefile]'.
****************************************************************************/
static void sg_save_savefile(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* Save savefile options. */
  sg_save_savefile_options(saving, savefile_options_default);

  secfile_insert_int(saving->file, compat[compat_current].version, "savefile.version");

  /* Save reason of the savefile generation. */
  secfile_insert_str(saving->file, saving->save_reason, "savefile.reason");

  /* Save rulesetdir at this point as this ruleset is required by this
   * savefile. */
  secfile_insert_str(saving->file, game.server.rulesetdir, "savefile.rulesetdir");

  /* Save improvement order in savegame, so we are not dependent on ruleset
   * order. If the game isn't started improvements aren't loaded so we can
   * not save the order. */
  secfile_insert_int(saving->file, improvement_count(),
                     "savefile.improvement_size");
  if (improvement_count() > 0) {
    const char* buf[improvement_count()];

    improvement_iterate(pimprove) {
      buf[improvement_index(pimprove)] = improvement_rule_name(pimprove);
    } improvement_iterate_end;

    secfile_insert_str_vec(saving->file, buf, improvement_count(),
                           "savefile.improvement_vector");
  }

  /* Save technology order in savegame, so we are not dependent on ruleset
   * order. If the game isn't started advances aren't loaded so we can not
   * save the order. */
  secfile_insert_int(saving->file, game.control.num_tech_types,
                     "savefile.technology_size");
  if (game.control.num_tech_types > 0) {
    const char* buf[game.control.num_tech_types];

    buf[A_NONE] = "A_NONE";
    advance_iterate(A_FIRST, a) {
      buf[advance_index(a)] = advance_rule_name(a);
    } advance_iterate_end;
    secfile_insert_str_vec(saving->file, buf, game.control.num_tech_types,
                           "savefile.technology_vector");
  }

  /* Save specials order in savegame. */
  secfile_insert_int(saving->file, S_LAST, "savefile.specials_size");
  {
    const char **modname;

    modname = fc_calloc(S_LAST, sizeof(*modname));
    tile_special_type_iterate(j) {
      modname[j] = special_rule_name(j);
    } tile_special_type_iterate_end;

    secfile_insert_str_vec(saving->file, modname, S_LAST,
                           "savefile.specials_vector");
    free(modname);
  }

  /* Save bases order in the savegame. */
  secfile_insert_int(saving->file, game.control.num_base_types,
                     "savefile.bases_size");
  if (game.control.num_base_types > 0) {
    const char **modname;
    int i = 0;

    modname = fc_calloc(game.control.num_base_types, sizeof(*modname));

    base_type_iterate(pbase) {
      modname[i++] = base_rule_name(pbase);
    } base_type_iterate_end;

    secfile_insert_str_vec(saving->file, modname,
                           game.control.num_base_types,
                           "savefile.bases_vector");
    free(modname);
  }

  /* Save roads order in the savegame. */
  secfile_insert_int(saving->file, game.control.num_road_types,
                     "savefile.roads_size");
  if (game.control.num_road_types > 0) {
    const char **modname;
    int i = 0;

    modname = fc_calloc(game.control.num_road_types, sizeof(*modname));

    road_type_iterate(proad) {
      modname[i++] = road_rule_name(proad);
    } road_type_iterate_end;

    secfile_insert_str_vec(saving->file, modname,
                           game.control.num_road_types,
                           "savefile.roads_vector");
    free(modname);
  }
}

/****************************************************************************
  Save options for this savegame. sg_load_savefile_options() is not defined.
****************************************************************************/
static void sg_save_savefile_options(struct savedata *saving,
                                     const char *option)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (option == NULL) {
    /* no additional option */
    return;
  }

  sz_strlcat(saving->secfile_options, option);
  secfile_replace_str(saving->file, saving->secfile_options,
                      "savefile.options");
}

/* =======================================================================
 * Load / save game status.
 * ======================================================================= */

/****************************************************************************
  Load '[game]'.
****************************************************************************/
static void sg_load_game(struct loaddata *loading)
{
  int game_version;
  const char *string;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* Load version. */
  game_version
    = secfile_lookup_int_default(loading->file, 0, "game.version");
  /* We require at least version 2.2.99 */
  sg_failure_ret(20299 <= game_version, "Saved game is too old, at least "
                                        "version 2.2.99 required.");

  /* Load server state. */
  string = secfile_lookup_str_default(loading->file, "S_S_INITIAL",
                                      "game.server_state");
  loading->server_state = server_states_by_name(string, strcmp);
  if (!server_states_is_valid(loading->server_state)) {
    /* Don't take any risk! */
    loading->server_state = S_S_INITIAL;
  }

  string = secfile_lookup_str_default(loading->file,
                                      default_meta_patches_string(),
                                      "game.meta_patches");
  set_meta_patches_string(string);
  game.server.meta_info.user_message_set
    = secfile_lookup_bool_default(loading->file, FALSE,
                                  "game.meta_usermessage");
  if (game.server.meta_info.user_message_set) {
    string = secfile_lookup_str_default(loading->file,
                                        default_meta_message_string(),
                                        "game.meta_message");
    set_user_meta_message_string(string);
  }

  if (0 == strcmp(DEFAULT_META_SERVER_ADDR, srvarg.metaserver_addr)) {
    /* Do not overwrite this if the user requested a specific metaserver
     * from the command line (option --Metaserver). */
    sz_strlcpy(srvarg.metaserver_addr,
               secfile_lookup_str_default(loading->file,
                                          DEFAULT_META_SERVER_ADDR,
                                          "game.meta_server"));
  }

  if ('\0' == srvarg.serverid[0]) {
    /* Do not overwrite this if the user requested a specific metaserver
     * from the command line (option --serverid). */
    sz_strlcpy(srvarg.serverid,
               secfile_lookup_str_default(loading->file, "",
                                          "game.serverid"));
  }
  sz_strlcpy(server.game_identifier,
             secfile_lookup_str_default(loading->file, "", "game.id"));
  /* We are not checking game_identifier legality just yet.
   * That's done when we are sure that rand seed has been initialized,
   * so that we can generate new game_identifier, if needed.
   * See sq_load_sanitycheck(). */

  game.info.skill_level
    = secfile_lookup_int_default(loading->file, GAME_DEFAULT_SKILL_LEVEL,
                                 "game.skill_level");
  game.info.phase_mode
    = secfile_lookup_int_default(loading->file, GAME_DEFAULT_PHASE_MODE,
                                 "game.phase_mode");
  game.server.phase_mode_stored
    = secfile_lookup_int_default(loading->file, GAME_DEFAULT_PHASE_MODE,
                                 "game.phase_mode_stored");
  game.server.scoreturn
    = secfile_lookup_int_default(loading->file,
                                 game.info.turn + GAME_DEFAULT_SCORETURN,
                                 "game.scoreturn");

  game.server.timeoutint
    = secfile_lookup_int_default(loading->file, GAME_DEFAULT_TIMEOUTINT,
                                 "game.timeoutint");
  game.server.timeoutintinc
    = secfile_lookup_int_default(loading->file, GAME_DEFAULT_TIMEOUTINTINC,
                                 "game.timeoutintinc");
  game.server.timeoutinc
    = secfile_lookup_int_default(loading->file, GAME_DEFAULT_TIMEOUTINC,
                                 "game.timeoutinc");
  game.server.timeoutincmult
    = secfile_lookup_int_default(loading->file, GAME_DEFAULT_TIMEOUTINCMULT,
                                 "game.timeoutincmult");
  game.server.timeoutcounter
    = secfile_lookup_int_default(loading->file, GAME_DEFAULT_TIMEOUTCOUNTER,
                                 "game.timeoutcounter");

  game.info.turn
    = secfile_lookup_int_default(loading->file, 0, "game.turn");
  sg_failure_ret(secfile_lookup_int(loading->file, &game.info.year,
                                    "game.year"), "%s", secfile_error());
  game.info.year_0_hack
    = secfile_lookup_bool_default(loading->file, FALSE, "game.year_0_hack");

  game.info.globalwarming
    = secfile_lookup_int_default(loading->file, 0, "game.globalwarming");
  game.info.heating
    = secfile_lookup_int_default(loading->file, 0, "game.heating");
  game.info.warminglevel
    = secfile_lookup_int_default(loading->file, 0, "game.warminglevel");

  game.info.nuclearwinter
    = secfile_lookup_int_default(loading->file, 0, "game.nuclearwinter");
  game.info.cooling
    = secfile_lookup_int_default(loading->file, 0, "game.cooling");
  game.info.coolinglevel
    = secfile_lookup_int_default(loading->file, 0, "game.coolinglevel");

  game.info.is_new_game
    = !secfile_lookup_bool_default(loading->file, TRUE, "game.save_players");
}

/****************************************************************************
  Save '[game]'.
****************************************************************************/
static void sg_save_game(struct savedata *saving)
{
  int game_version;
  const char *user_message;
  enum server_states srv_state;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  game_version = MAJOR_VERSION *10000 + MINOR_VERSION *100 + PATCH_VERSION;
  secfile_insert_int(saving->file, game_version, "game.version");

  /* Game state: once the game is no longer a new game (ie, has been
   * started the first time), it should always be considered a running
   * game for savegame purposes. */
  if (saving->scenario && !game.scenario.players) {
    srv_state = S_S_INITIAL;
  } else {
    srv_state = game.info.is_new_game ? server_state() : S_S_RUNNING;
  }
  secfile_insert_str(saving->file, server_states_name(srv_state),
                     "game.server_state");

  secfile_insert_str(saving->file, get_meta_patches_string(),
                     "game.meta_patches");
  secfile_insert_bool(saving->file, game.server.meta_info.user_message_set,
                      "game.meta_usermessage");
  user_message = get_user_meta_message_string();
  if (user_message != NULL) {
    secfile_insert_str(saving->file, user_message, "game.meta_message");
  }
  secfile_insert_str(saving->file, meta_addr_port(), "game.meta_server");

  secfile_insert_str(saving->file, server.game_identifier, "game.id");
  secfile_insert_str(saving->file, srvarg.serverid, "game.serverid");

  secfile_insert_int(saving->file, game.info.skill_level,
                     "game.skill_level");
  secfile_insert_int(saving->file, game.info.phase_mode,
                     "game.phase_mode");
  secfile_insert_int(saving->file, game.server.phase_mode_stored,
                     "game.phase_mode_stored");
  secfile_insert_int(saving->file, game.server.scoreturn,
                     "game.scoreturn");

  secfile_insert_int(saving->file, game.server.timeoutint,
                     "game.timeoutint");
  secfile_insert_int(saving->file, game.server.timeoutintinc,
                     "game.timeoutintinc");
  secfile_insert_int(saving->file, game.server.timeoutinc,
                     "game.timeoutinc");
  secfile_insert_int(saving->file, game.server.timeoutincmult,
                     "game.timeoutincmult");
  secfile_insert_int(saving->file, game.server.timeoutcounter,
                     "game.timeoutcounter");

  secfile_insert_int(saving->file, game.info.turn, "game.turn");
  secfile_insert_int(saving->file, game.info.year, "game.year");
  secfile_insert_bool(saving->file, game.info.year_0_hack,
                      "game.year_0_hack");

  secfile_insert_int(saving->file, game.info.globalwarming,
                     "game.globalwarming");
  secfile_insert_int(saving->file, game.info.heating,
                     "game.heating");
  secfile_insert_int(saving->file, game.info.warminglevel,
                     "game.warminglevel");

  secfile_insert_int(saving->file, game.info.nuclearwinter,
                     "game.nuclearwinter");
  secfile_insert_int(saving->file, game.info.cooling,
                     "game.cooling");
  secfile_insert_int(saving->file, game.info.coolinglevel,
                     "game.coolinglevel");

  if (!game_was_started()) {
    saving->save_players = FALSE;
  } else if (saving->scenario) {
    saving->save_players = game.scenario.players;
  } else {
    saving->save_players = TRUE;
  }
  secfile_insert_bool(saving->file, saving->save_players,
                      "game.save_players");
}

/* =======================================================================
 * Load / save random status.
 * ======================================================================= */

/****************************************************************************
  Load '[random]'.
****************************************************************************/
static void sg_load_random(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (secfile_lookup_bool_default(loading->file, FALSE, "random.save")) {
    const char *string;
    int i;

    sg_failure_ret(secfile_lookup_int(loading->file, &loading->rstate.j,
                                      "random.index_J"), "%s", secfile_error());
    sg_failure_ret(secfile_lookup_int(loading->file, &loading->rstate.k,
                                      "random.index_K"), "%s", secfile_error());
    sg_failure_ret(secfile_lookup_int(loading->file, &loading->rstate.x,
                                      "random.index_X"), "%s", secfile_error());

    for (i = 0; i < 8; i++) {
      string = secfile_lookup_str(loading->file, "random.table%d",i);
      sg_failure_ret(NULL != string, "%s", secfile_error());
      sscanf(string, "%8x %8x %8x %8x %8x %8x %8x", &loading->rstate.v[7*i],
             &loading->rstate.v[7*i+1], &loading->rstate.v[7*i+2],
             &loading->rstate.v[7*i+3], &loading->rstate.v[7*i+4],
             &loading->rstate.v[7*i+5], &loading->rstate.v[7*i+6]);
    }
    loading->rstate.is_init = TRUE;
    fc_rand_set_state(loading->rstate);
  } else {
    /* No random values - mark the setting. */
    (void) secfile_lookup_bool_default(loading->file, TRUE, "random.save");

    /* We're loading a game without a seed (which is okay, if it's a scenario).
     * We need to generate the game seed now because it will be needed later
     * during the load. */
    init_game_seed();
    loading->rstate = fc_rand_state();
  }
}

/****************************************************************************
  Save '[random]'.
****************************************************************************/
static void sg_save_random(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (fc_rand_is_init() && game.server.save_options.save_random) {
    int i;
    RANDOM_STATE rstate = fc_rand_state();

    secfile_insert_bool(saving->file, TRUE, "random.save");
    fc_assert(rstate.is_init);

    secfile_insert_int(saving->file, rstate.j, "random.index_J");
    secfile_insert_int(saving->file, rstate.k, "random.index_K");
    secfile_insert_int(saving->file, rstate.x, "random.index_X");

    for (i = 0; i < 8; i++) {
      char vec[100];

      fc_snprintf(vec, sizeof(vec),
                  "%8x %8x %8x %8x %8x %8x %8x", rstate.v[7 * i],
                  rstate.v[7 * i + 1], rstate.v[7 * i + 2],
                  rstate.v[7 * i + 3], rstate.v[7 * i + 4],
                  rstate.v[7 * i + 5], rstate.v[7 * i + 6]);
      secfile_insert_str(saving->file, vec, "random.table%d", i);
    }
  } else {
    secfile_insert_bool(saving->file, FALSE, "random.save");
  }
}

/* =======================================================================
 * Load / save lua script data.
 * ======================================================================= */

/****************************************************************************
  Load '[script]'.
****************************************************************************/
static void sg_load_script(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  script_server_state_load(loading->file);
}

/****************************************************************************
  Save '[script]'.
****************************************************************************/
static void sg_save_script(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  script_server_state_save(saving->file);
}

/* =======================================================================
 * Load / save scenario data.
 * ======================================================================= */

/****************************************************************************
  Load '[scenario]'.
****************************************************************************/
static void sg_load_scenario(struct loaddata *loading)
{
  const char *buf;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (NULL == secfile_section_lookup(loading->file, "scenario")) {
    /* Nothing to do. */
    return;
  }

  buf = secfile_lookup_str_default(loading->file, "", "scenario.name");
  if (buf[0] != '\0') {
    game.scenario.is_scenario = TRUE;
    sz_strlcpy(game.scenario.name, buf);
    buf = secfile_lookup_str_default(loading->file, "",
                                     "scenario.description");
    if (buf[0] != '\0') {
      sz_strlcpy(game.scenario.description, buf);
    } else {
      game.scenario.description[0] = '\0';
    }
    game.scenario.players
      = secfile_lookup_bool_default(loading->file, TRUE, "scenario.players");

    sg_failure_ret(loading->server_state == S_S_INITIAL
                   || (loading->server_state == S_S_RUNNING
                       && game.scenario.players == TRUE),
                   "Invalid scenario definition (server state '%s' and "
                   "players are %s).",
                   server_states_name(loading->server_state),
                   game.scenario.players ? "saved" : "not saved");
  } else {
    game.scenario.is_scenario = FALSE;
  }

  if (game.scenario.is_scenario) {
    /* Remove all defined players. They are recreated with the skill level
     * defined by the scenario. */
    aifill(0);
  }
}

/****************************************************************************
  Save '[scenario]'.
****************************************************************************/
static void sg_save_scenario(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (!saving->scenario && !game.scenario.is_scenario) {
    return;
  }

  secfile_insert_str(saving->file, game.scenario.name, "scenario.name");
  secfile_insert_str(saving->file, game.scenario.description,
                     "scenario.description");
  secfile_insert_bool(saving->file, game.scenario.players, "scenario.players");
}

/* =======================================================================
 * Load / save game settings.
 * ======================================================================= */

/****************************************************************************
  Load '[settings]'.
****************************************************************************/
static void sg_load_settings(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  settings_game_load(loading->file, "settings");

  /* Save current status of fogofwar. */
  game.server.fogofwar_old = game.info.fogofwar;

  /* Add all compatibility settings here. */
}

/****************************************************************************
  Save [settings].
****************************************************************************/
static void sg_save_settings(struct savedata *saving)
{
  enum map_generator real_generator = map.server.generator;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (saving->scenario) {
    map.server.generator = MAPGEN_SCENARIO; /* We want a scenario. */
  }
  settings_game_save(saving->file, "settings");
  /* Restore real map generator. */
  map.server.generator = real_generator;

  /* Add all compatibility settings here. */
}

/* =======================================================================
 * Load / save the main map.
 * ======================================================================= */

/****************************************************************************
  Fill main map road vectors with roads indicated by (old savegame) specials
****************************************************************************/
void mainmap_specials_to_roads(void)
{
  struct road_type *proad = road_by_special(S_OLD_ROAD);
  struct road_type *prail = road_by_special(S_OLD_RAILROAD);

  whole_map_iterate(ptile) {
    if (tile_has_special(ptile, S_OLD_ROAD)) {
      tile_add_road(ptile, proad);
    }
    if (tile_has_special(ptile, S_OLD_RAILROAD)) {
      tile_add_road(ptile, prail);
    }
  } whole_map_iterate_end;
}

/****************************************************************************
  Fill player map road vectors with roads indicated by (old savegame) specials
****************************************************************************/
void plrmap_specials_to_roads(struct player *plr)
{
  struct road_type *proad = road_by_special(S_OLD_ROAD);
  struct road_type *prail = road_by_special(S_OLD_RAILROAD);
  int road_no = -1;
  int rail_no = -1;

  if (proad != NULL) {
    road_no = road_number(proad);
  }
  if (prail != NULL) {
    rail_no = road_number(prail);
  }

  whole_map_iterate(ptile) {
    struct player_tile *pptile = map_get_player_tile(ptile, plr);

    if (contains_special(pptile->special, S_OLD_ROAD) && road_no >= 0) {
      BV_SET(pptile->roads, road_no);
    }
    if (contains_special(pptile->special, S_OLD_RAILROAD) && rail_no >= 0) {
      BV_SET(pptile->roads, rail_no);
    }
  } whole_map_iterate_end;
}

/****************************************************************************
  Load '[map'].
****************************************************************************/
static void sg_load_map(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  map.server.have_huts
    = secfile_lookup_bool_default(loading->file, TRUE, "map.have_huts");

  if (S_S_INITIAL == loading->server_state
      && MAPGEN_SCENARIO == map.server.generator) {
    /* Generator MAPGEN_SCENARIO is used;
     * this map was done with the map editor. */

    /* Load tiles. */
    sg_load_map_tiles(loading);
    sg_load_map_startpos(loading);
    sg_load_map_tiles_bases(loading);
    sg_load_map_tiles_roads(loading);
    if (has_capability("specials", loading->secfile_options)) {
      /* Load specials and resources. */
      sg_load_map_tiles_specials(loading, FALSE);
      sg_load_map_tiles_resources(loading);
    } else if (has_capability("riversoverlay", loading->secfile_options)) {
      /* Load only rivers overlay. */
      sg_load_map_tiles_specials(loading, TRUE);
      map.server.have_rivers_overlay = TRUE;
    }

    /* Nothing more needed for a scenario. */
    return;
  }

  if (S_S_INITIAL == loading->server_state) {
    /* Nothing more to do if it is not a scenario but in initial state. */
    return;
  }

  sg_load_map_tiles(loading);
  sg_load_map_startpos(loading);
  sg_load_map_tiles_bases(loading);
  sg_load_map_tiles_roads(loading);
  sg_load_map_tiles_specials(loading, FALSE);
  sg_load_map_tiles_resources(loading);
  sg_load_map_known(loading);
  sg_load_map_owner(loading);
  sg_load_map_worked(loading);
}

/****************************************************************************
  Save 'map'.
****************************************************************************/
static void sg_save_map(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  secfile_insert_bool(saving->file, map.server.have_huts, "map.have_huts");

  if (map_is_empty()) {
    /* No map. */
    return;
  }

  sg_save_map_tiles(saving);
  sg_save_map_startpos(saving);
  sg_save_map_tiles_bases(saving);
  sg_save_map_tiles_roads(saving);
  if (!map.server.have_resources) {
    if (map.server.have_rivers_overlay) {
      /* Save the rivers overlay map; this is a special case to allow
       * re-saving scenarios which have rivers overlay data. This only
       * applies if you don't have the rest of the specials. */
      sg_save_savefile_options(saving, " riversoverlay");
      sg_save_map_tiles_specials(saving, TRUE);
    }
  } else {
    sg_save_savefile_options(saving, " specials");
    sg_save_map_tiles_specials(saving, FALSE);
    sg_save_map_tiles_resources(saving);
  }

  sg_save_map_owner(saving);
  sg_save_map_worked(saving);
  sg_save_map_known(saving);
}

/****************************************************************************
  ...
****************************************************************************/
static void sg_load_map_tiles(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* With a FALSE parameter [xy]size are not changed by this call. */
  map_init_topology(FALSE);

  /* Allocate map. */
  map_allocate();

  /* get the terrain type */
  LOAD_MAP_CHAR(ch, ptile, ptile->terrain = char2terrain(ch), loading->file,
                "map.t%04d");
  assign_continent_numbers();

  /* Check for special tile sprites. */
  whole_map_iterate(ptile) {
    const char *spec_sprite;
    const char *label;
    int nat_x, nat_y;

    index_to_native_pos(&nat_x, &nat_y, tile_index(ptile));
    spec_sprite = secfile_lookup_str(loading->file, "map.spec_sprite_%d_%d",
                                     nat_x, nat_y);
    label = secfile_lookup_str_default(loading->file, NULL, "map.label_%d_%d",
                                       nat_x, nat_y);
    if (NULL != ptile->spec_sprite) {
      ptile->spec_sprite = fc_strdup(spec_sprite);
    }
    if (label != NULL) {
      tile_set_label(ptile, label);
    }
  } whole_map_iterate_end;
}

/****************************************************************************
  Save all map tiles
****************************************************************************/
static void sg_save_map_tiles(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* Save the terrain type. */
  SAVE_MAP_CHAR(ptile, terrain2char(ptile->terrain), saving->file,
                "map.t%04d");

  /* Save special tile sprites. */
  whole_map_iterate(ptile) {
    int nat_x, nat_y;

    index_to_native_pos(&nat_x, &nat_y, tile_index(ptile));
    if (ptile->spec_sprite) {
      secfile_insert_str(saving->file, ptile->spec_sprite,
                         "map.spec_sprite_%d_%d", nat_x, nat_y);
    }
    if (ptile->label != NULL) {
      secfile_insert_str(saving->file, ptile->label,
                         "map.label_%d_%d", nat_x, nat_y);
    }
  } whole_map_iterate_end;
}

/****************************************************************************
  Load bases to map
****************************************************************************/
static void sg_load_map_tiles_bases(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* Load bases. */
  halfbyte_iterate_bases(j, loading->base.size) {
    LOAD_MAP_CHAR(ch, ptile, sg_bases_set(&ptile->bases, ch,
                                          loading->base.order + 4 * j),
                  loading->file, "map.b%02d_%04d", j);
  } halfbyte_iterate_bases_end;
}

/****************************************************************************
  Save information about bases on map
****************************************************************************/
static void sg_save_map_tiles_bases(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* Save bases. */
  halfbyte_iterate_bases(j, game.control.num_base_types) {
    int mod[4];
    int l;

    for (l = 0; l < 4; l++) {
      if (4 * j + 1 > game.control.num_base_types) {
        mod[l] = -1;
      } else {
        mod[l] = 4 * j + l;
      }
    }
    SAVE_MAP_CHAR(ptile, sg_bases_get(ptile->bases, mod), saving->file,
                  "map.b%02d_%04d", j);
  } halfbyte_iterate_bases_end;
}

/****************************************************************************
  Load roads to map
****************************************************************************/
static void sg_load_map_tiles_roads(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* Load roads. */
  halfbyte_iterate_roads(j, loading->road.size) {
    LOAD_MAP_CHAR(ch, ptile, sg_roads_set(&ptile->roads, ch,
                                          loading->road.order + 4 * j),
                  loading->file, "map.r%02d_%04d", j);
  } halfbyte_iterate_roads_end;
}

/****************************************************************************
  Save information about roads on map
****************************************************************************/
static void sg_save_map_tiles_roads(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* Save roads. */
  halfbyte_iterate_roads(j, game.control.num_road_types) {
    int mod[4];
    int l;

    for (l = 0; l < 4; l++) {
      if (4 * j + 1 > game.control.num_road_types) {
        mod[l] = -1;
      } else {
        mod[l] = 4 * j + l;
      }
    }
    SAVE_MAP_CHAR(ptile, sg_roads_get(ptile->roads, mod), saving->file,
                  "map.r%02d_%04d", j);
  } halfbyte_iterate_roads_end;
}

/****************************************************************************
  Load information about specials on map
****************************************************************************/
static void sg_load_map_tiles_specials(struct loaddata *loading,
                                       bool rivers_overlay)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* If 'rivers_overlay' is set to TRUE, load only the rivers overlay map
   * from the savegame file.
   *
   * A scenario may define the terrain of the map but not list the specials
   * on it (thus allowing users to control the placement of specials).
   * However rivers are a special case and must be included in the map along
   * with the scenario. Thus in those cases this function should be called
   * to load the river information separate from any other special data.
   *
   * This does not need to be called from map_load(), because map_load()
   * loads the rivers overlay along with the rest of the specials.  Call this
   * only if you've already called map_load_tiles(), and want to load only
   * the rivers overlay but no other specials. Scenarios that encode things
   * this way should have the "riversoverlay" capability. */
  halfbyte_iterate_special(j, loading->special.size) {
    LOAD_MAP_CHAR(ch, ptile, sg_special_set(&ptile->special, ch,
                                            loading->special.order + 4 * j,
                                            rivers_overlay),
                  loading->file, "map.spe%02d_%04d", j);
  } halfbyte_iterate_special_end;
}

/****************************************************************************
  Save information about specials on map.
****************************************************************************/
static void sg_save_map_tiles_specials(struct savedata *saving,
                                       bool rivers_overlay)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  halfbyte_iterate_special(j, S_LAST) {
    enum tile_special_type mod[4];
    int l;

    for (l = 0; l < 4; l++) {
      if (rivers_overlay) {
        /* Save only rivers overlay. */
        mod[l] = (4 * j + l == S_RIVER) ? S_RIVER : S_LAST;
      } else {
        /* Save all specials. */
        mod[l] = MIN(4 * j + l, S_LAST);
      }
    }
    SAVE_MAP_CHAR(ptile, sg_special_get(ptile->special, mod), saving->file,
                  "map.spe%02d_%04d", j);
  } halfbyte_iterate_special_end;
}

/****************************************************************************
  Load information about resources on map.
****************************************************************************/
static void sg_load_map_tiles_resources(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  LOAD_MAP_CHAR(ch, ptile, ptile->resource = char2resource(ch),
                loading->file, "map.res%04d");

  /* After the resources are loaded, indicate those currently valid. */
  whole_map_iterate(ptile) {
    if (NULL == ptile->resource || NULL == ptile->terrain) {
      continue;
    }

    if (terrain_has_resource(ptile->terrain, ptile->resource)) {
      /* cannot use set_special() for internal values */
      BV_SET(ptile->special, S_RESOURCE_VALID);
    }
  } whole_map_iterate_end;

  map.server.have_resources = TRUE;
}

/****************************************************************************
  Load information about resources on map.
****************************************************************************/
static void sg_save_map_tiles_resources(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  SAVE_MAP_CHAR(ptile, resource2char(ptile->resource), saving->file,
                "map.res%04d");
}

/****************************************************************************
  Load starting positions for the players from a savegame file. There should
  be at least enough for every player.
****************************************************************************/
static void sg_load_map_startpos(struct loaddata *loading)
{
  struct nation_type *pnation;
  struct startpos *psp;
  struct tile *ptile;
  const char SEPARATOR = '#';
  const char *nation_names;
  int nat_x, nat_y;
  bool exclude;
  int i, startpos_count;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  startpos_count
    = secfile_lookup_int_default(loading->file, 0, "map.startpos_count");

  if (0 == startpos_count) {
    /* Nothing to do. */
    return;
  }

  for (i = 0; i < startpos_count; i++) {
    if (!secfile_lookup_int(loading->file, &nat_x, "map.startpos%d.x", i)
        || !secfile_lookup_int(loading->file, &nat_y,
                               "map.startpos%d.y", i)) {
      log_sg("Warning: Undefined coordinates for startpos %d", i);
      continue;
    }

    ptile = native_pos_to_tile(nat_x, nat_y);
    if (NULL == ptile) {
      log_error("Start position native coordinates (%d, %d) do not exist "
                "in this map. Skipping...", nat_x, nat_y);
      continue;
    }

    exclude = secfile_lookup_bool_default(loading->file, FALSE,
                                          "map.startpos%d.exclude", i);

    psp = map_startpos_new(ptile);

    nation_names = secfile_lookup_str(loading->file,
                                      "map.startpos%d.nations", i);
    if (NULL != nation_names && '\0' != nation_names[0]) {
      const size_t size = strlen(nation_names) + 1;
      char buf[size], *start, *end;

      memcpy(buf, nation_names, size);
      for (start = buf - 1; NULL != start; start = end) {
        start++;
        if ((end = strchr(start, SEPARATOR))) {
          *end = '\0';
        }

        pnation = nation_by_rule_name(start);
        if (NO_NATION_SELECTED != pnation) {
          if (exclude) {
            startpos_disallow(psp, pnation);
          } else {
            startpos_allow(psp, pnation);
          }
        } else {
          log_verbose("Missing nation \"%s\".", start);
        }
      }
    }
  }

  if (0 < map_startpos_count()
      && map_startpos_count() < game.server.max_players) {
    log_verbose("Number of starts (%d) are lower than rules.max_players "
                "(%d), lowering rules.max_players.",
                map_startpos_count(), game.server.max_players);
    game.server.max_players = map_startpos_count();
  }
}

/****************************************************************************
  Save the map start positions.
****************************************************************************/
static void sg_save_map_startpos(struct savedata *saving)
{
  struct tile *ptile;
  const char SEPARATOR = '#';
  int i = 0;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (!game.server.save_options.save_starts) {
    return;
  }

  secfile_insert_int(saving->file, map_startpos_count(),
                     "map.startpos_count");

  map_startpos_iterate(psp) {
    int nat_x, nat_y;

    ptile = startpos_tile(psp);

    index_to_native_pos(&nat_x, &nat_y, tile_index(ptile));
    secfile_insert_int(saving->file, nat_x, "map.startpos%d.x", i);
    secfile_insert_int(saving->file, nat_y, "map.startpos%d.y", i);

    secfile_insert_bool(saving->file, startpos_is_excluding(psp),
                        "map.startpos%d.exclude", i);
    if (startpos_allows_all(psp)) {
      secfile_insert_str(saving->file, "", "map.startpos%d.nations", i);
    } else {
      const struct nation_hash *nations = startpos_raw_nations(psp);
      char nation_names[MAX_LEN_NAME * nation_hash_size(nations)];

      nation_names[0] = '\0';
      nation_hash_iterate(nations, pnation) {
        if ('\0' == nation_names[0]) {
          fc_strlcpy(nation_names, nation_rule_name(pnation),
                     sizeof(nation_names));
        } else {
          cat_snprintf(nation_names, sizeof(nation_names),
                       "%c%s", SEPARATOR, nation_rule_name(pnation));
        }
      } nation_hash_iterate_end;
      secfile_insert_str(saving->file, nation_names,
                         "map.startpos%d.nations", i);
    }
    i++;
  } map_startpos_iterate_end;

  fc_assert(map_startpos_count() == i);
}

/****************************************************************************
  Load tile owner information
****************************************************************************/
static void sg_load_map_owner(struct loaddata *loading)
{
  int x, y;
  struct player *owner = NULL;
  struct tile *claimer = NULL;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (game.info.is_new_game) {
    /* No owner/source information for a new game / scenario. */
    return;
  }

  /* Owner and ownership source are stored as plain numbers */
  for (y = 0; y < map.ysize; y++) {
    const char *buffer1 = secfile_lookup_str(loading->file,
                                             "map.owner%04d", y);
    const char *buffer2 = secfile_lookup_str(loading->file,
                                             "map.source%04d", y);
    const char *ptr1 = buffer1;
    const char *ptr2 = buffer2;

    sg_failure_ret(buffer1 != NULL, "%s", secfile_error());
    sg_failure_ret(buffer2 != NULL, "%s", secfile_error());

    for (x = 0; x < map.xsize; x++) {
      char token1[TOKEN_SIZE];
      char token2[TOKEN_SIZE];
      int number;
      struct tile *ptile = native_pos_to_tile(x, y);

      scanin(&ptr1, ",", token1, sizeof(token1));
      sg_failure_ret(token1[0] != '\0',
                     "Map size not correct (map.owner%d).", y);
      if (strcmp(token1, "-") == 0) {
        owner = NULL;
      } else {
        sg_failure_ret(str_to_int(token1, &number),
                       "Got map owner %s in (%d, %d).", token1, x, y);
        owner = player_by_number(number);
      }

      scanin(&ptr2, ",", token2, sizeof(token2));
      sg_failure_ret(token2[0] != '\0',
                     "Map size not correct (map.source%d).", y);
      if (strcmp(token2, "-") == 0) {
        claimer = NULL;
      } else {
        sg_failure_ret(str_to_int(token2, &number),
                       "Got map source %s in (%d, %d).", token2, x, y);
        claimer = index_to_tile(number);
      }

      map_claim_ownership(ptile, owner, claimer);
    }
  }
}

/****************************************************************************
  Save tile owner information
****************************************************************************/
static void sg_save_map_owner(struct savedata *saving)
{
  int x, y;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (saving->scenario && !saving->save_players) {
    /* Nothing to do for a scenario without saved players. */
    return;
  }

  /* Store owner and ownership source as plain numbers. */
  for (y = 0; y < map.ysize; y++) {
    char line[map.xsize * TOKEN_SIZE];

    line[0] = '\0';
    for (x = 0; x < map.xsize; x++) {
      char token[TOKEN_SIZE];
      struct tile *ptile = native_pos_to_tile(x, y);

      if (!saving->save_players || tile_owner(ptile) == NULL) {
        strcpy(token, "-");
      } else {
        fc_snprintf(token, sizeof(token), "%d",
                    player_number(tile_owner(ptile)));
      }
      strcat(line, token);
      if (x + 1 < map.xsize) {
        strcat(line, ",");
      }
    }
    secfile_insert_str(saving->file, line, "map.owner%04d", y);
  }

  for (y = 0; y < map.ysize; y++) {
    char line[map.xsize * TOKEN_SIZE];

    line[0] = '\0';
    for (x = 0; x < map.xsize; x++) {
      char token[TOKEN_SIZE];
      struct tile *ptile = native_pos_to_tile(x, y);

      if (ptile->claimer == NULL) {
        strcpy(token, "-");
      } else {
        fc_snprintf(token, sizeof(token), "%d", tile_index(ptile->claimer));
      }
      strcat(line, token);
      if (x + 1 < map.xsize) {
        strcat(line, ",");
      }
    }
    secfile_insert_str(saving->file, line, "map.source%04d", y);
  }
}

/****************************************************************************
  Save worked tiles information
****************************************************************************/
static void sg_load_map_worked(struct loaddata *loading)
{
  int x, y;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  sg_failure_ret(loading->worked_tiles == NULL,
                 "City worked map not loaded!");

  loading->worked_tiles = fc_malloc(MAP_INDEX_SIZE *
                                    sizeof(*loading->worked_tiles));

  for (y = 0; y < map.ysize; y++) {
    const char *buffer = secfile_lookup_str(loading->file, "map.worked%04d",
                                            y);
    const char *ptr = buffer;

    sg_failure_ret(NULL != buffer,
                   "Savegame corrupt - map line %d not found.", y);
    for (x = 0; x < map.xsize; x++) {
      char token[TOKEN_SIZE];
      int number;
      struct tile *ptile = native_pos_to_tile(x, y);

      scanin(&ptr, ",", token, sizeof(token));
      sg_failure_ret('\0' != token[0],
                     "Savegame corrupt - map size not correct.");
      if (strcmp(token, "-") == 0) {
        number = -1;
      } else {
        sg_failure_ret(str_to_int(token, &number) && 0 < number,
                       "Savegame corrupt - got tile worked by city "
                       "id=%s in (%d, %d).", token, x, y);
      }

      loading->worked_tiles[ptile->index] = number;
    }
  }
}

/****************************************************************************
  Save worked tiles information
****************************************************************************/
static void sg_save_map_worked(struct savedata *saving)
{
  int x, y;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (saving->scenario && !saving->save_players) {
    /* Nothing to do for a scenario without saved players. */
    return;
  }

  /* additionally save the tiles worked by the cities */
  for (y = 0; y < map.ysize; y++) {
    char line[map.xsize * TOKEN_SIZE];

    line[0] = '\0';
    for (x = 0; x < map.xsize; x++) {
      char token[TOKEN_SIZE];
      struct tile *ptile = native_pos_to_tile(x, y);
      struct city *pcity = tile_worked(ptile);

      if (pcity == NULL) {
        strcpy(token, "-");
      } else {
        fc_snprintf(token, sizeof(token), "%d", pcity->id);
      }
      strcat(line, token);
      if (x < map.xsize) {
        strcat(line, ",");
      }
    }
    secfile_insert_str(saving->file, line, "map.worked%04d", y);
  }
}

/****************************************************************************
  Load tile known status
****************************************************************************/
static void sg_load_map_known(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  players_iterate(pplayer) {
    /* Allocate player private map here; it is needed in different modules
     * besides this one ((i.e. sg_load_player_*()). */
    player_map_init(pplayer);
  } players_iterate_end;

  if (secfile_lookup_bool_default(loading->file, TRUE,
                                  "game.save_known")) {
    int lines = player_slot_max_used_number()/32 + 1, j, p, l, i;
    unsigned int *known = fc_calloc(lines * MAP_INDEX_SIZE, sizeof(*known));

    for (l = 0; l < lines; l++) {
      for (j = 0; j < 8; j++) {
        for (i = 0; i < 4; i++) {
          /* Only bother trying to load the map for this halfbyte if at least
           * one of the corresponding player slots is in use. */
          if (player_slot_is_used(player_slot_by_number(l*32 + j*4 + i))) {
            LOAD_MAP_CHAR(ch, ptile,
                          known[l * MAP_INDEX_SIZE + tile_index(ptile)]
                            |= ascii_hex2bin(ch, j),
                          loading->file, "map.k%02d_%04d", l * 8 + j);
            break;
          }
        }
      }
    }

    players_iterate(pplayer) {
      dbv_clr_all(&pplayer->tile_known);
    } players_iterate_end;

    /* HACK: we read the known data from hex into 32-bit integers, and
     * now we convert it to the known tile data of each player. */
    whole_map_iterate(ptile) {
      players_iterate(pplayer) {
        p = player_index(pplayer);
        l = player_index(pplayer) / 32;

        if (known[l * MAP_INDEX_SIZE + tile_index(ptile)] & (1u << (p % 32))) {
          map_set_known(ptile, pplayer);
        }
      } players_iterate_end;
    } whole_map_iterate_end;

    FC_FREE(known);
  }
}

/****************************************************************************
  Save tile known status for whole map and all players
****************************************************************************/
static void sg_save_map_known(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (!saving->save_players) {
    secfile_insert_bool(saving->file, FALSE, "game.save_known");
    return;
  } else {
    int lines = player_slot_max_used_number()/32 + 1;

    secfile_insert_bool(saving->file, game.server.save_options.save_known,
                        "game.save_known");
    if (game.server.save_options.save_known) {
      int j, p, l, i;
      unsigned int *known = fc_calloc(lines * MAP_INDEX_SIZE, sizeof(*known));

      /* HACK: we convert the data into a 32-bit integer, and then save it as
       * hex. */

      whole_map_iterate(ptile) {
        players_iterate(pplayer) {
          if (map_is_known(ptile, pplayer)) {
            p = player_index(pplayer);
            l = p / 32;
            known[l * MAP_INDEX_SIZE + tile_index(ptile)]
              |= (1u << (p % 32)); /* "p % 32" = "p - l * 32" */ 
          }
        } players_iterate_end;
      } whole_map_iterate_end;

      for (l = 0; l < lines; l++) {
        for (j = 0; j < 8; j++) {
          for (i = 0; i < 4; i++) {
            /* Only bother saving the map for this halfbyte if at least one
             * of the corresponding player slots is in use */
            if (player_slot_is_used(player_slot_by_number(l*32 + j*4 + i))) {
              /* put 4-bit segments of the 32-bit "known" field */
              SAVE_MAP_CHAR(ptile, bin2ascii_hex(known[l * MAP_INDEX_SIZE
                                                       + tile_index(ptile)], j),
                            saving->file, "map.k%02d_%04d", l * 8 + j);
              break;
            }
          }
        }
      }

      FC_FREE(known);
    }
  }
}

/* =======================================================================
 * Load / save player data.
 *
 * This is splitted into two parts as some data can only be loaded if the
 * number of players is known and the corresponding player slots are
 * defined.
 * ======================================================================= */

/****************************************************************************
  Load '[player]' (basic data).
****************************************************************************/
static void sg_load_players_basic(struct loaddata *loading)
{
  int i, k, nplayers;
  const char *string;
  bool shuffle_loaded = TRUE;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (S_S_INITIAL == loading->server_state
      || game.info.is_new_game) {
    /* Nothing more to do. */
    return;
  }

  /* Load destroyed wonders: */
  string = secfile_lookup_str(loading->file,
                              "players.destroyed_wonders");
  sg_failure_ret(string != NULL, "%s", secfile_error());
  sg_failure_ret(strlen(string) == loading->improvement.size,
                 "Invalid length for 'players.destroyed_wonders' "
                 "(%lu ~= %lu)", (unsigned long) strlen(string),
                 (unsigned long) loading->improvement.size);
  for (k = 0; k < loading->improvement.size; k++) {
    sg_failure_ret(string[k] == '1' || string[k] == '0',
                   "Undefined value '%c' within "
                   "'players.destroyed_wonders'.", string[k]);

    if (string[k] == '1') {
      struct impr_type *pimprove =
          improvement_by_rule_name(loading->improvement.order[k]);
      if (pimprove) {
        game.info.great_wonder_owners[improvement_index(pimprove)]
          = WONDER_DESTROYED;
      }
    }
  }

  server.identity_number
    = secfile_lookup_int_default(loading->file, server.identity_number,
                                 "players.identity_number_used");

  /* Initialize nations we loaded from rulesets. This has to be after
   * map loading and before we seek nations for players */
  init_available_nations();

  /* First remove all defined players. */
  players_iterate(pplayer) {
    server_remove_player(pplayer);
  } players_iterate_end;

  /* Now, load the players from the savefile. */
  player_slots_iterate(pslot) {
    struct player *pplayer;
    struct rgbcolor *prgbcolor = NULL;
    int pslot_id = player_slot_index(pslot);

    if (NULL == secfile_section_lookup(loading->file, "player%d",
                                       pslot_id)) {
      continue;
    }

    /* Get player AI type. */
    string = secfile_lookup_str(loading->file, "player%d.ai_type",
                                player_slot_index(pslot));
    sg_failure_ret(string != NULL, "%s", secfile_error());

    /* Get player color */
    if (!rgbcolor_load(loading->file, &prgbcolor, "player%d.color",
                       pslot_id)) {
      log_verbose("No color defined for player %d.", pslot_id);
    }

    /* Create player. */
    pplayer = server_create_player(player_slot_index(pslot), string,
                                   prgbcolor);
    sg_failure_ret(pplayer != NULL, "Invalid AI type: '%s'!", string);

    server_player_init(pplayer, FALSE, FALSE);

    /* Free the color definition. */
    rgbcolor_destroy(prgbcolor);
  } player_slots_iterate_end;

  /* check number of players */
  nplayers = secfile_lookup_int_default(loading->file, 0, "players.nplayers");
  sg_failure_ret(player_count() == nplayers, "The value of players.nplayers "
                 "(%d) from the loaded game does not match the number of "
                 "players present (%d).", nplayers, player_count());

  /* Load team informations. */
  players_iterate(pplayer) {
    int team;
    struct team_slot *tslot = NULL;

    sg_failure_ret(secfile_lookup_int(loading->file, &team,
                                      "player%d.team_no",
                                      player_number(pplayer))
                   && (tslot = team_slot_by_number(team)),
                   "Invalid team definition for player %s (nb %d).",
                   player_name(pplayer), player_number(pplayer));
    team_add_player(pplayer, team_new(tslot));
  } players_iterate_end;

  /* Loading the shuffle list is quite complex. At the time of saving the
   * shuffle data is saved as
   *   shuffled_player_<number> = player_slot_id
   * where number is an increasing number and player_slot_id is a number
   * between 0 and the maximum number of player slots. Now we have to create
   * a list
   *   shuffler_players[number] = player_slot_id
   * where all player slot IDs are used exactly one time. The code below
   * handles this ... */
  if (secfile_lookup_int_default(loading->file, -1,
                                 "players.shuffled_player_%d", 0) >= 0) {
    int shuffled_players[player_slot_count()];
    bool shuffled_player_set[player_slot_count()];

    player_slots_iterate(pslot) {
      int plrid = player_slot_index(pslot);

      /* Array to save used numbers. */
      shuffled_player_set[plrid] = FALSE;
      /* List of all player IDs (needed for set_shuffled_players()). It is
       * initialised with the value -1 to indicate that no value is set. */
      shuffled_players[plrid] = -1;
    } player_slots_iterate_end;

    /* Load shuffled player list. */
    for (i = 0; i < player_count(); i++){
      int shuffle
        = secfile_lookup_int_default(loading->file, -1,
                                     "players.shuffled_player_%d", i);

      if (shuffle == -1) {
        log_sg("Missing player shuffle information (index %d) "
               "- reshuffle player list!", i);
        shuffle_loaded = FALSE;
        break;
      } else if (shuffled_player_set[shuffle]) {
        log_sg("Player shuffle %d used two times "
               "- reshuffle player list!", shuffle);
        shuffle_loaded = FALSE;
        break;
      }
      /* Set this ID as used. */
      shuffled_player_set[shuffle] = TRUE;

      /* Save the player ID in the shuffle list. */
      shuffled_players[i] = shuffle;
    }

    if (shuffle_loaded) {
      /* Insert missing numbers. */
      int shuffle_index = player_count();
      for (i = 0; i < player_slot_count(); i++){
        if (!shuffled_player_set[i]) {
          shuffled_players[shuffle_index] = i;
          shuffle_index++;
        }
        /* shuffle_index must not grow behind the size of shuffled_players. */
        sg_failure_ret(shuffle_index <= player_slot_count(),
                       "Invalid player shuffle data!");
      }

#ifdef DEBUG
      log_debug("[load shuffle] player_count() = %d", player_count());
      player_slots_iterate(pslot) {
        int plrid = player_slot_index(pslot);
        log_debug("[load shuffle] id: %3d => slot: %3d | slot %3d: %s",
                  plrid, shuffled_players[plrid], plrid,
                  shuffled_player_set[plrid] ? "is used" : "-");
      } player_slots_iterate_end;
#endif /* DEBUG */

      /* Set shuffle list from savegame. */
      set_shuffled_players(shuffled_players);
    }
  }

  if (!shuffle_loaded) {
    /* No shuffled players included or error loading them, so shuffle them
     * (this may include scenarios). */
    shuffle_players();
  }
}

/****************************************************************************
  Load '[player]'.
****************************************************************************/
static void sg_load_players(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (game.info.is_new_game) {
    /* Nothing to do. */
    return;
  }

  players_iterate(pplayer) {
    sg_load_player_main(loading, pplayer);
    sg_load_player_cities(loading, pplayer);
    sg_load_player_units(loading, pplayer);
    sg_load_player_attributes(loading, pplayer);

    /* Check the sucess of the functions above. */
    sg_check_ret();

    /* print out some informations */
    if (pplayer->ai_controlled) {
      log_normal(_("%s has been added as %s level AI-controlled player "
                   "(%s)."), player_name(pplayer),
                 ai_level_name(pplayer->ai_common.skill_level),
                 ai_name(pplayer->ai));
    } else {
      log_normal(_("%s has been added as human player."),
                 player_name(pplayer));
    }
  } players_iterate_end;

  /* In case of tech_leakage, we can update research only after all the 
   * players have been loaded */
  /* Also load the transport status of the units here. It must be a special
   * case as all units must be known (unit on an allied transporter). */
  players_iterate(pplayer) {
    /* Mark the reachable techs */
    player_research_update(pplayer);
    /* Load unit transport status. */
    sg_load_player_units_transport(loading, pplayer);
  } players_iterate_end;

  /* Some players may have invalid nations in the ruleset. Once all players
   * are loaded, pick one of the remaining nations for them. */
  players_iterate(pplayer) {
    if (pplayer->nation == NO_NATION_SELECTED) {
      player_set_nation(pplayer, pick_a_nation(NULL, FALSE, TRUE,
                                               NOT_A_BARBARIAN));
      /* TRANS: Minor error message: <Leader> ... <Poles>. */
      log_sg(_("%s had invalid nation; changing to %s."),
             player_name(pplayer), nation_plural_for_player(pplayer));
    }
  } players_iterate_end;

  /* Sanity check alliances, prevent allied-with-ally-of-enemy. */
  players_iterate_alive(plr) {
    players_iterate_alive(aplayer) {
      if (pplayers_allied(plr, aplayer)) {
        enum dipl_reason can_ally = pplayer_can_make_treaty(plr, aplayer,
                                                            DS_ALLIANCE);
        if (can_ally == DIPL_ALLIANCE_PROBLEM_US
            || can_ally == DIPL_ALLIANCE_PROBLEM_THEM) {
          log_sg("Illegal alliance structure detected: "
                 "%s alliance to %s reduced to peace treaty.",
                 nation_rule_name(nation_of_player(plr)),
                 nation_rule_name(nation_of_player(aplayer)));
          player_diplstate_get(plr, aplayer)->type = DS_PEACE;
          player_diplstate_get(aplayer, plr)->type = DS_PEACE;
        }
      }
    } players_iterate_alive_end;
  } players_iterate_alive_end;

  /* Update all city information.  This must come after all cities are
   * loaded (in player_load) but before player (dumb) cities are loaded
   * in player_load_vision(). */
  cities_iterate(pcity) {
    city_refresh_from_main_map(pcity, NULL);
  } cities_iterate_end;

  /* Since the cities must be placed on the map to put them on the
     player map we do this afterwards */
  players_iterate(pplayer) {
    sg_load_player_vision(loading, pplayer);
    plrmap_specials_to_roads(pplayer);
    /* Check the sucess of the function above. */
    sg_check_ret();
  } players_iterate_end;

  /* Check shared vision. */
  players_iterate(pplayer) {
    BV_CLR_ALL(pplayer->gives_shared_vision);
    BV_CLR_ALL(pplayer->server.really_gives_vision);
  } players_iterate_end;

  players_iterate(pplayer) {
    int plr1 = player_index(pplayer);

    players_iterate(pplayer2) {
      int plr2 = player_index(pplayer2);
      if (secfile_lookup_bool_default(loading->file, FALSE,
              "player%d.diplstate%d.gives_shared_vision", plr1, plr2)) {
        give_shared_vision(pplayer, pplayer2);
      }
    } players_iterate_end;
  } players_iterate_end;

  initialize_globals();
  unit_ordering_apply();

  /* All vision is ready; this calls city_thaw_workers_queue(). */
  map_calculate_borders();

  /* Make sure everything is consistent. */
  players_iterate(pplayer) {
    unit_list_iterate(pplayer->units, punit) {
      if (!can_unit_continue_current_activity(punit)) {
        log_sg("Unit doing illegal activity in savegame!");
        punit->activity = ACTIVITY_IDLE;
      }
    } unit_list_iterate_end;
  } players_iterate_end;

  cities_iterate(pcity) {
    city_refresh(pcity);
    city_thaw_workers(pcity); /* may auto_arrange_workers() */
  } cities_iterate_end;
}

/****************************************************************************
  Save '[player]'.
****************************************************************************/
static void sg_save_players(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if ((saving->scenario && !saving->save_players)
      || !game_was_started()) {
    /* Nothing to do for a scenario without saved players or a game in
     * INITIAL state. */
    return;
  }

  secfile_insert_int(saving->file, player_count(), "players.nplayers");

  /* Save destroyed wonders as bitvector. Note that improvement order
   * is saved in 'savefile.improvement.order'. */
  {
    char destroyed[B_LAST+1];

    improvement_iterate(pimprove) {
      if (is_great_wonder(pimprove)
          && great_wonder_is_destroyed(pimprove)) {
        destroyed[improvement_index(pimprove)] = '1';
      } else {
        destroyed[improvement_index(pimprove)] = '0';
      }
    } improvement_iterate_end;
    destroyed[improvement_count()] = '\0';
    secfile_insert_str(saving->file, destroyed,
                       "players.destroyed_wonders");
  }

  secfile_insert_int(saving->file, server.identity_number,
                     "players.identity_number_used");

  /* Save player order. */
  {
    int i = 0;
    shuffled_players_iterate(pplayer) {
      secfile_insert_int(saving->file, player_number(pplayer),
                         "players.shuffled_player_%d", i);
      i++;
    } shuffled_players_iterate_end;
  }

  /* Sort units. */
  unit_ordering_calc();

  /* Save players. */
  players_iterate(pplayer) {
    sg_save_player_main(saving, pplayer);
    sg_save_player_cities(saving, pplayer);
    sg_save_player_units(saving, pplayer);
    sg_save_player_attributes(saving, pplayer);
    sg_save_player_vision(saving, pplayer);
  } players_iterate_end;
}

/****************************************************************************
  Main player data loading function
****************************************************************************/
static void sg_load_player_main(struct loaddata *loading,
                                struct player *plr)
{
  int i, plrno = player_number(plr);
  const char *string;
  struct government *gov;
  struct player_research *research;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* Basic player data. */
  string = secfile_lookup_str(loading->file, "player%d.name", plrno);
  sg_failure_ret(string != NULL, "%s", secfile_error());
  server_player_set_name(plr, string);
  sz_strlcpy(plr->username,
             secfile_lookup_str_default(loading->file, "",
                                        "player%d.username", plrno));
  sz_strlcpy(plr->ranked_username,
             secfile_lookup_str_default(loading->file, "",
                                        "player%d.ranked_username",
                                        plrno));
  string = secfile_lookup_str_default(loading->file, "",
                                      "player%d.delegation_username",
                                      plrno);
  /* Defaults to no delegation. */
  if (strlen(string)) {
    player_delegation_set(plr, string);
  }

  /* Nation */
  string = secfile_lookup_str(loading->file, "player%d.nation", plrno);
  player_set_nation(plr, nation_by_rule_name(string));

  /* Government */
  string = secfile_lookup_str(loading->file, "player%d.government_name",
                              plrno);
  gov = government_by_rule_name(string);
  sg_failure_ret(gov != NULL, "Player%d: unsupported government \"%s\".",
                 plrno, string);
  plr->government = gov;

  /* Target government */
  string = secfile_lookup_str(loading->file,
                              "player%d.target_government_name", plrno);
  if (string) {
    plr->target_government = government_by_rule_name(string);
  } else {
    plr->target_government = NULL;
  }
  plr->revolution_finishes
    = secfile_lookup_int_default(loading->file, -1,
                                 "player%d.revolution_finishes", plrno);

  sg_failure_ret(secfile_lookup_bool(loading->file, &plr->server.capital,
                                     "player%d.capital", plrno),
                 "%s", secfile_error());


  sg_failure_ret(secfile_lookup_bool(loading->file, &plr->ai_controlled,
                                     "player%d.ai.control", plrno),
                 "%s", secfile_error());

  /* Load diplomatic data (diplstate + embassy + vision).
   * Shared vision is loaded in sg_load_players(). */
  BV_CLR_ALL(plr->real_embassy);
  players_iterate(pplayer) {
    char buf[32];
    struct player_diplstate *ds = player_diplstate_get(plr, pplayer);
    i = player_index(pplayer);

    /* load diplomatic status */
    fc_snprintf(buf, sizeof(buf), "player%d.diplstate%d", plrno, i);

    ds->type =
      secfile_lookup_int_default(loading->file, DS_WAR, "%s.type", buf);
    ds->max_state =
      secfile_lookup_int_default(loading->file, DS_WAR, "%s.max_state", buf);
    ds->first_contact_turn =
      secfile_lookup_int_default(loading->file, 0,
                                 "%s.first_contact_turn", buf);
    ds->turns_left =
      secfile_lookup_int_default(loading->file, -2, "%s.turns_left", buf);
    ds->has_reason_to_cancel =
      secfile_lookup_int_default(loading->file, 0,
                                 "%s.has_reason_to_cancel", buf);
    ds->contact_turns_left =
      secfile_lookup_int_default(loading->file, 0,
                                 "%s.contact_turns_left", buf);

    if (secfile_lookup_bool_default(loading->file, FALSE, "%s.embassy",
                                    buf)) {
      BV_SET(plr->real_embassy, i);
    }
    /* 'gives_shared_vision' is loaded in sg_load_players() as all cities
     * must be known. */
  } players_iterate_end;

  /* load ai data */
  players_iterate(aplayer) {
    char buf[32];

    fc_snprintf(buf, sizeof(buf), "player%d.ai%d", plrno,
                player_index(aplayer));

    plr->ai_common.love[player_index(aplayer)] =
        secfile_lookup_int_default(loading->file, 1, "%s.love", buf);
  } players_iterate_end;

  CALL_FUNC_EACH_AI(player_load, plr, loading->file, plrno);

  /* Some sane defaults */
  BV_CLR_ALL(plr->ai_common.handicaps);
  plr->ai_common.fuzzy = 0;
  plr->ai_common.expand = 100;
  plr->ai_common.science_cost = 100;
  plr->ai_common.skill_level =
    secfile_lookup_int_default(loading->file, game.info.skill_level,
                               "player%d.ai.skill_level", plrno);

  plr->ai_common.barbarian_type
    = secfile_lookup_int_default(loading->file, 0,
                                 "player%d.ai.is_barbarian", plrno);
  if (is_barbarian(plr)) {
    server.nbarbarians++;
  }

  if (plr->ai_controlled) {
    set_ai_level_directer(plr, plr->ai_common.skill_level);
    CALL_PLR_AI_FUNC(gained_control, plr, plr);
  }

  /* Load city style. */
  {
    int city_style;

    string = secfile_lookup_str(loading->file, "player%d.city_style_by_name",
                                plrno);
    sg_failure_ret(string != NULL, "%s", secfile_error());
    city_style = city_style_by_rule_name(string);
    if (city_style < 0) {
      log_sg("Player%d: unsupported city_style_name \"%s\". "
             "Changed to \"%s\".", plrno, string, city_style_rule_name(0));
      city_style = 0;
    }
    plr->city_style = city_style;
  }

  plr->nturns_idle = 0;
  plr->is_male = secfile_lookup_bool_default(loading->file, TRUE,
                                             "player%d.is_male", plrno);
  sg_failure_ret(secfile_lookup_bool(loading->file, &plr->is_alive,
                                     "player%d.is_alive", plrno),
                 "%s", secfile_error());
  sg_failure_ret(secfile_lookup_int(loading->file, &plr->economic.gold,
                                    "player%d.gold", plrno),
                 "%s", secfile_error());
  sg_failure_ret(secfile_lookup_int(loading->file, &plr->economic.tax,
                                    "player%d.rates.tax", plrno),
                 "%s", secfile_error());
  sg_failure_ret(secfile_lookup_int(loading->file, &plr->economic.science,
                                    "player%d.rates.science", plrno),
                 "%s", secfile_error());
  sg_failure_ret(secfile_lookup_int(loading->file, &plr->economic.luxury,
                                    "player%d.rates.luxury", plrno),
                 "%s", secfile_error());

  /* Add techs from game and nation, but ignore game.info.tech. */
  init_tech(plr, FALSE);

  /* Load research related data. */
  research = player_research_get(plr);

  research->tech_goal =
    technology_load(loading->file, "player%d.research.goal", plrno);
  plr->bulbs_last_turn =
    secfile_lookup_int_default(loading->file, 0,
                               "player%d.research.bulbs_last_turn", plrno);
  sg_failure_ret(secfile_lookup_int(loading->file,
                                    &research->techs_researched,
                                    "player%d.research.techs", plrno),
                 "%s", secfile_error());
  sg_failure_ret(secfile_lookup_int(loading->file,
                                    &research->future_tech,
                                    "player%d.research.futuretech", plrno),
                 "%s", secfile_error());
  sg_failure_ret(secfile_lookup_int(loading->file,
                                    &research->bulbs_researched,
                                    "player%d.research.bulbs", plrno),
                 "%s", secfile_error());
  research->bulbs_researching_saved
    = secfile_lookup_int_default(loading->file, 0,
                                 "player%d.research.bulbs_before", plrno);
  research->researching_saved
    = technology_load(loading->file, "player%d.research.saved", plrno);
  research->researching
    = technology_load(loading->file, "player%d.research.now", plrno);
  research->got_tech
    = secfile_lookup_bool_default(loading->file, FALSE,
                                  "player%d.research.got_tech", plrno);

  string = secfile_lookup_str(loading->file, "player%d.research.done", plrno);
  sg_failure_ret(string != NULL, "%s", secfile_error());
  sg_failure_ret(strlen(string) == loading->technology.size,
                 "Invalid length of 'player%d.technology' (%lu ~= %lu).",
                 plrno, (unsigned long) strlen(string),
                 (unsigned long) loading->technology.size);
  for (i = 0; i < loading->technology.size; i++) {
    sg_failure_ret(string[i] == '1' || string[i] == '0',
                   "Undefined value '%c' within 'player%d.technology'.",
                   string[i], plrno)

    if (string[i] == '1') {
      struct advance *padvance =
          advance_by_rule_name(loading->technology.order[i]);
      if (padvance) {
        player_invention_set(plr, advance_number(padvance), TECH_KNOWN);
      }
    }
  }

  /* Unit statistics. */
  plr->score.units_built =
      secfile_lookup_int_default(loading->file, 0,
                                 "player%d.units_built", plrno);
  plr->score.units_killed =
      secfile_lookup_int_default(loading->file, 0,
                                 "player%d.units_killed", plrno);
  plr->score.units_lost =
      secfile_lookup_int_default(loading->file, 0,
                                 "player%d.units_lost", plrno);

  /* Load space ship data. */
  {
    struct player_spaceship *ship = &plr->spaceship;
    char prefix[32];
    const char *st;
    int ei;

    fc_snprintf(prefix, sizeof(prefix), "player%d.spaceship", plrno);
    spaceship_init(ship);
    sg_failure_ret(secfile_lookup_int(loading->file,
                                      &ei,
                                      "%s.state", prefix),
                   "%s", secfile_error());
    ship->state = ei;

    if (ship->state != SSHIP_NONE) {
      sg_failure_ret(secfile_lookup_int(loading->file, &ship->structurals,
                                        "%s.structurals", prefix),
                     "%s", secfile_error());
      sg_failure_ret(secfile_lookup_int(loading->file, &ship->components,
                                 "%s.components", prefix),
                     "%s", secfile_error());
      sg_failure_ret(secfile_lookup_int(loading->file, &ship->modules,
                                 "%s.modules", prefix),
                     "%s", secfile_error());
      sg_failure_ret(secfile_lookup_int(loading->file, &ship->fuel,
                                 "%s.fuel", prefix),
                     "%s", secfile_error());
      sg_failure_ret(secfile_lookup_int(loading->file, &ship->propulsion,
                                 "%s.propulsion", prefix),
                     "%s", secfile_error());
      sg_failure_ret(secfile_lookup_int(loading->file, &ship->habitation,
                                 "%s.habitation", prefix),
                     "%s", secfile_error());
      sg_failure_ret(secfile_lookup_int(loading->file, &ship->life_support,
                                 "%s.life_support", prefix),
                     "%s", secfile_error());
      sg_failure_ret(secfile_lookup_int(loading->file, &ship->solar_panels,
                                 "%s.solar_panels", prefix),
                     "%s", secfile_error());

      st = secfile_lookup_str(loading->file, "%s.structure", prefix);
      sg_failure_ret(st != NULL, "%s", secfile_error())
      for (i = 0; i < NUM_SS_STRUCTURALS && st[i]; i++) {
        sg_failure_ret(st[i] == '1' || st[i] == '0',
                       "Undefined value '%c' within '%s.structure'.", st[i],
                       prefix)

        if (st[i] == '0') {
          ship->structure[i] = FALSE;
        } else {
          ship->structure[i] = TRUE;
        }
      }
      if (ship->state >= SSHIP_LAUNCHED) {
        sg_failure_ret(secfile_lookup_int(loading->file, &ship->launch_year,
                                          "%s.launch_year", prefix),
                       "%s", secfile_error());
      }
      spaceship_calc_derived(ship);
    }
  }

  /* Load lost wonder data. */
  string = secfile_lookup_str(loading->file, "player%d.lost_wonders", plrno);
  /* If not present, probably an old savegame; nothing to be done */
  if (string) {
    int k;
    sg_failure_ret(strlen(string) == loading->improvement.size,
                   "Invalid length for 'player%d.lost_wonders' "
                   "(%lu ~= %lu)", plrno, (unsigned long) strlen(string),
                   (unsigned long) loading->improvement.size);
    for (k = 0; k < loading->improvement.size; k++) {
      sg_failure_ret(string[k] == '1' || string[k] == '0',
                     "Undefined value '%c' within "
                     "'player%d.lost_wonders'.", plrno, string[k]);

      if (string[k] == '1') {
        struct impr_type *pimprove =
            improvement_by_rule_name(loading->improvement.order[k]);
        if (pimprove) {
          plr->wonders[improvement_index(pimprove)] = WONDER_LOST;
        }
      }
    }
  }
}

/****************************************************************************
  Main player data saving function.
****************************************************************************/
static void sg_save_player_main(struct savedata *saving,
                                struct player *plr)
{
  int i, plrno = player_number(plr);
  struct player_spaceship *ship = &plr->spaceship;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  secfile_insert_str(saving->file, ai_name(plr->ai),
                     "player%d.ai_type", plrno);
  secfile_insert_str(saving->file, player_name(plr),
                     "player%d.name", plrno);
  secfile_insert_str(saving->file, plr->username,
                     "player%d.username", plrno);
  rgbcolor_save(saving->file, plr->rgb, "player%d.color", plrno);
  secfile_insert_str(saving->file, plr->ranked_username,
                     "player%d.ranked_username", plrno);
  secfile_insert_str(saving->file,
                     player_delegation_get(plr) ? player_delegation_get(plr)
                                                : "",
                     "player%d.delegation_username", plrno);
  secfile_insert_str(saving->file, nation_rule_name(nation_of_player(plr)),
                     "player%d.nation", plrno);
  secfile_insert_int(saving->file, plr->team ? team_index(plr->team) : -1,
                     "player%d.team_no", plrno);

  secfile_insert_str(saving->file,
                     government_rule_name(government_of_player(plr)),
                     "player%d.government_name", plrno);
  if (plr->target_government) {
    secfile_insert_str(saving->file,
                       government_rule_name(plr->target_government),
                       "player%d.target_government_name", plrno);
  }

  secfile_insert_str(saving->file, city_style_rule_name(plr->city_style),
                      "player%d.city_style_by_name", plrno);

  secfile_insert_bool(saving->file, plr->is_male,
                      "player%d.is_male", plrno);
  secfile_insert_bool(saving->file, plr->is_alive,
                      "player%d.is_alive", plrno);
  secfile_insert_bool(saving->file, plr->ai_controlled,
                      "player%d.ai.control", plrno);

  players_iterate(pplayer) {
    char buf[32];
    struct player_diplstate *ds = player_diplstate_get(plr, pplayer);

    i = player_index(pplayer);

    /* save diplomatic state */
    fc_snprintf(buf, sizeof(buf), "player%d.diplstate%d", plrno, i);

    secfile_insert_int(saving->file, ds->type,
                       "%s.type", buf);
    secfile_insert_int(saving->file, ds->max_state,
                       "%s.max_state", buf);
    secfile_insert_int(saving->file, ds->first_contact_turn,
                       "%s.first_contact_turn", buf);
    secfile_insert_int(saving->file, ds->turns_left,
                       "%s.turns_left", buf);
    secfile_insert_int(saving->file, ds->has_reason_to_cancel,
                       "%s.has_reason_to_cancel", buf);
    secfile_insert_int(saving->file, ds->contact_turns_left,
                       "%s.contact_turns_left", buf);
    secfile_insert_bool(saving->file, player_has_real_embassy(plr, pplayer),
                        "%s.embassy", buf);
    secfile_insert_bool(saving->file, gives_shared_vision(plr, pplayer),
                        "%s.gives_shared_vision", buf);
  } players_iterate_end;

  players_iterate(aplayer) {
    i = player_index(aplayer);
    /* save ai data */
    secfile_insert_int(saving->file, plr->ai_common.love[i],
                       "player%d.ai%d.love", plrno, i);
  } players_iterate_end;

  CALL_FUNC_EACH_AI(player_save, plr, saving->file, plrno);

  secfile_insert_int(saving->file, plr->ai_common.skill_level,
                     "player%d.ai.skill_level", plrno);
  secfile_insert_int(saving->file, plr->ai_common.barbarian_type,
                     "player%d.ai.is_barbarian", plrno);
  secfile_insert_int(saving->file, plr->economic.gold,
                     "player%d.gold", plrno);
  secfile_insert_int(saving->file, plr->economic.tax,
                     "player%d.rates.tax", plrno);
  secfile_insert_int(saving->file, plr->economic.science,
                     "player%d.rates.science", plrno);
  secfile_insert_int(saving->file, plr->economic.luxury,
                     "player%d.rates.luxury", plrno);

  technology_save(saving->file, "player%d.research.goal",
                  plrno, player_research_get(plr)->tech_goal);
  secfile_insert_int(saving->file, plr->bulbs_last_turn,
                     "player%d.research.bulbs_last_turn", plrno);
  secfile_insert_int(saving->file,
                     player_research_get(plr)->techs_researched,
                     "player%d.research.techs", plrno);
  secfile_insert_int(saving->file, player_research_get(plr)->future_tech,
                     "player%d.research.futuretech", plrno);
  secfile_insert_int(saving->file,
                     player_research_get(plr)->bulbs_researching_saved,
                     "player%d.research.bulbs_before", plrno);
  technology_save(saving->file, "player%d.research.saved", plrno,
                  player_research_get(plr)->researching_saved);
  secfile_insert_int(saving->file,
                     player_research_get(plr)->bulbs_researched,
                     "player%d.research.bulbs", plrno);
  technology_save(saving->file, "player%d.research.now", plrno,
                  player_research_get(plr)->researching);
  secfile_insert_bool(saving->file, player_research_get(plr)->got_tech,
                      "player%d.research.got_tech", plrno);

  /* Save technology lists as bytevector. Note that technology order is
   * saved in savefile.technology.order */
  {
    char invs[A_LAST+1];
    advance_index_iterate(A_NONE, tech_id) {
      invs[tech_id] = (player_invention_state(plr, tech_id) == TECH_KNOWN)
                      ? '1' : '0';
    } advance_index_iterate_end;
    invs[game.control.num_tech_types] = '\0';
    secfile_insert_str(saving->file, invs, "player%d.research.done", plrno);
  }

  secfile_insert_bool(saving->file, plr->server.capital,
                      "player%d.capital", plrno);
  secfile_insert_int(saving->file, plr->revolution_finishes,
                     "player%d.revolution_finishes", plrno);

  /* Unit statistics. */
  secfile_insert_int(saving->file, plr->score.units_built,
                     "player%d.units_built", plrno);
  secfile_insert_int(saving->file, plr->score.units_killed,
                     "player%d.units_killed", plrno);
  secfile_insert_int(saving->file, plr->score.units_lost,
                     "player%d.units_lost", plrno);

  /* Save space ship status. */
  secfile_insert_int(saving->file, ship->state, "player%d.spaceship.state",
                     plrno);
  if (ship->state != SSHIP_NONE) {
    char buf[32];
    char st[NUM_SS_STRUCTURALS+1];
    int i;

    fc_snprintf(buf, sizeof(buf), "player%d.spaceship", plrno);

    secfile_insert_int(saving->file, ship->structurals,
                       "%s.structurals", buf);
    secfile_insert_int(saving->file, ship->components,
                       "%s.components", buf);
    secfile_insert_int(saving->file, ship->modules,
                       "%s.modules", buf);
    secfile_insert_int(saving->file, ship->fuel, "%s.fuel", buf);
    secfile_insert_int(saving->file, ship->propulsion, "%s.propulsion", buf);
    secfile_insert_int(saving->file, ship->habitation, "%s.habitation", buf);
    secfile_insert_int(saving->file, ship->life_support,
                       "%s.life_support", buf);
    secfile_insert_int(saving->file, ship->solar_panels,
                       "%s.solar_panels", buf);

    for(i = 0; i < NUM_SS_STRUCTURALS; i++) {
      st[i] = (ship->structure[i]) ? '1' : '0';
    }
    st[i] = '\0';
    secfile_insert_str(saving->file, st, "%s.structure", buf);
    if (ship->state >= SSHIP_LAUNCHED) {
      secfile_insert_int(saving->file, ship->launch_year,
                         "%s.launch_year", buf);
    }
  }

  /* Save lost wonders info. */
  {
    char lost[B_LAST+1];

    improvement_iterate(pimprove) {
      if (is_wonder(pimprove) && wonder_is_lost(plr, pimprove)) {
        lost[improvement_index(pimprove)] = '1';
      } else {
        lost[improvement_index(pimprove)] = '0';
      }
    } improvement_iterate_end;
    lost[improvement_count()] = '\0';
    secfile_insert_str(saving->file, lost,
                       "player%d.lost_wonders", plrno);
  }
}

/****************************************************************************
  Load city data
****************************************************************************/
static void sg_load_player_cities(struct loaddata *loading,
                                  struct player *plr)
{
  int ncities, i, plrno = player_number(plr);

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  sg_failure_ret(secfile_lookup_int(loading->file, &ncities,
                                    "player%d.ncities", plrno),
                 "%s", secfile_error());

  if (!plr->is_alive && ncities > 0) {
    log_sg("'player%d.ncities' = %d for dead player!", plrno, ncities);
    ncities = 0;
  }

  /* Load all cities of the player. */
  for (i = 0; i < ncities; i++) {
    char buf[32];
    struct city *pcity;

    fc_snprintf(buf, sizeof(buf), "player%d.c%d", plrno, i);

    /* Create a dummy city. */
    pcity = create_city_virtual(plr, NULL, buf);
    adv_city_alloc(pcity);
    if (!sg_load_player_city(loading, plr, pcity, buf)) {
      adv_city_free(pcity);
      destroy_city_virtual(pcity);
      sg_failure_ret(FALSE, "Error loading city %d of player %d.", i, plrno);
    }

    identity_number_reserve(pcity->id);
    idex_register_city(pcity);

    /* Load the information about the nationality of citizens. This is done
     * here because the city sanity check called by citizens_update() requires
     * that the city is registered. */
    sg_load_player_city_citizens(loading, plr, pcity, buf);

    /* After everything is loaded, but before vision. */
    map_claim_ownership(city_tile(pcity), plr, city_tile(pcity));

    /* adding the city contribution to fog-of-war */
    pcity->server.vision = vision_new(plr, city_tile(pcity));
    vision_reveal_tiles(pcity->server.vision, game.server.vision_reveal_tiles);
    city_refresh_vision(pcity);

    /* Check the squared city radius. Must be after improvements, as the
     * effect City_Radius_SQ could be influenced by improvements; and after
     * the vision is defined, as the function calls city_refresh_vision(). */
    if (city_map_update_radius_sq(pcity, FALSE)) {
      /* squared city radius has been changed - repair the city */
      auto_arrange_workers(pcity);
    }

    city_list_append(plr->cities, pcity);
  }

  /* Check the sanity of the cities. */
  city_list_iterate(plr->cities, pcity) {
    city_refresh(pcity);
    sanity_check_city(pcity);
  } city_list_iterate_end;
}

/****************************************************************************
  Load data for one city. sg_save_player_city() is not defined.
****************************************************************************/
static bool sg_load_player_city(struct loaddata *loading, struct player *plr,
                                struct city *pcity, const char *citystr)
{
  struct player *past;
  const char *kind, *name, *string;
  int id, i, repair, specialists = 0, workers = 0, value;
  int nat_x, nat_y;
  citizens size;

  sg_warn_ret_val(secfile_lookup_int(loading->file, &nat_x, "%s.x", citystr),
                  FALSE, "%s", secfile_error());
  sg_warn_ret_val(secfile_lookup_int(loading->file, &nat_y, "%s.y", citystr),
                  FALSE, "%s", secfile_error());
  pcity->tile = native_pos_to_tile(nat_x, nat_y);
  sg_warn_ret_val(NULL != pcity->tile, FALSE,
                  "%s has invalid center tile (%d, %d)",
                  citystr, nat_x, nat_y);
  sg_warn_ret_val(NULL == tile_city(pcity->tile), FALSE,
                  "%s duplicates city (%d, %d)", citystr, nat_x, nat_y);

  /* Instead of dying, use 'citystr' string for damaged name. */
  sz_strlcpy(pcity->name, secfile_lookup_str_default(loading->file, citystr,
                                                     "%s.name", citystr));

  sg_warn_ret_val(secfile_lookup_int(loading->file, &pcity->id, "%s.id",
                                     citystr), FALSE, "%s", secfile_error());

  id = secfile_lookup_int_default(loading->file, player_number(plr),
                                  "%s.original", citystr);
  past = player_by_number(id);
  if (NULL != past) {
    pcity->original = past;
  }

  sg_warn_ret_val(secfile_lookup_int(loading->file, &value, "%s.size",
                                     citystr), FALSE, "%s", secfile_error());
  size = (citizens)value; /* set the correct type */
  sg_warn_ret_val(value == (int)size, FALSE,
                  "Invalid city size: %d, set to %d", value, size);
  city_size_set(pcity, size);

  specialist_type_iterate(sp) {
    sg_warn_ret_val(
        secfile_lookup_int(loading->file, &value, "%s.n%s", citystr,
                           specialist_rule_name(specialist_by_number(sp))),
        FALSE, "%s", secfile_error());
    pcity->specialists[sp] = (citizens)value; /* set the correct type */
    sg_warn_ret_val(value == (int)pcity->specialists[sp], FALSE,
                    "Invalid number of specialists: %d, set to %d.", value,
                    pcity->specialists[sp]);
    specialists += pcity->specialists[sp];
  } specialist_type_iterate_end;

  for (i = 0; i < MAX_TRADE_ROUTES; i++) {
    pcity->trade[i] = secfile_lookup_int_default(loading->file, 0,
                                                 "%s.traderoute%d", citystr, i);
  }

  sg_warn_ret_val(secfile_lookup_int(loading->file, &pcity->food_stock,
                                    "%s.food_stock", citystr),
                  FALSE, "%s", secfile_error());
  sg_warn_ret_val(secfile_lookup_int(loading->file, &pcity->shield_stock,
                                    "%s.shield_stock", citystr),
                  FALSE, "%s", secfile_error());

  pcity->airlift =
    secfile_lookup_int_default(loading->file, 0, "%s.airlift", citystr);
  pcity->was_happy =
    secfile_lookup_bool_default(loading->file, FALSE, "%s.was_happy",
                                citystr);

  pcity->turn_plague =
    secfile_lookup_int_default(loading->file, 0, "%s.turn_plague", citystr);
  if (game.info.illness_on) {
    /* recalculate city illness */
    pcity->server.illness = city_illness_calc(pcity, NULL, NULL,
                                              &(pcity->illness_trade), NULL);
  }

  sg_warn_ret_val(secfile_lookup_int(loading->file, &pcity->anarchy,
                                    "%s.anarchy", citystr),
                  FALSE, "%s", secfile_error());
  pcity->rapture =
    secfile_lookup_int_default(loading->file, 0, "%s.rapture", citystr);
  pcity->server.steal =
    secfile_lookup_int_default(loading->file, 0, "%s.steal", citystr);

  /* before did_buy for undocumented hack */
  pcity->turn_founded =
    secfile_lookup_int_default(loading->file, -2, "%s.turn_founded",
                               citystr);
  sg_warn_ret_val(secfile_lookup_int(loading->file, &i, "%s.did_buy",
                                     citystr), FALSE, "%s", secfile_error());
  pcity->did_buy = (i != 0);
  if (i == -1 && pcity->turn_founded == -2) {
    /* undocumented hack */
    pcity->turn_founded = game.info.turn;
  }

  pcity->did_sell =
    secfile_lookup_bool_default(loading->file, FALSE, "%s.did_sell", citystr);

  sg_warn_ret_val(secfile_lookup_int(loading->file, &pcity->turn_last_built,
                                    "%s.turn_last_built", citystr),
                  FALSE, "%s", secfile_error());

  kind = secfile_lookup_str(loading->file, "%s.currently_building_kind",
                            citystr);
  name = secfile_lookup_str(loading->file, "%s.currently_building_name",
                            citystr);
  pcity->production = universal_by_rule_name(kind, name);
  sg_warn_ret_val(pcity->production.kind != universals_n_invalid(), FALSE,
                  "%s.currently_building: unknown \"%s\" \"%s\".",
                  citystr, kind, name);

  kind = secfile_lookup_str(loading->file, "%s.changed_from_kind",
                            citystr);
  name = secfile_lookup_str(loading->file, "%s.changed_from_name",
                            citystr);
  pcity->changed_from = universal_by_rule_name(kind, name);
  sg_warn_ret_val(pcity->changed_from.kind != universals_n_invalid(), FALSE,
                 "%s.changed_from: unknown \"%s\" \"%s\".",
                 citystr, kind, name);

  pcity->before_change_shields =
    secfile_lookup_int_default(loading->file, pcity->shield_stock,
                               "%s.before_change_shields", citystr);
  pcity->caravan_shields =
    secfile_lookup_int_default(loading->file, 0,
                               "%s.caravan_shields", citystr);
  pcity->disbanded_shields =
    secfile_lookup_int_default(loading->file, 0,
                               "%s.disbanded_shields", citystr);
  pcity->last_turns_shield_surplus =
    secfile_lookup_int_default(loading->file, 0,
                               "%s.last_turns_shield_surplus",
                               citystr);

  pcity->server.synced = FALSE; /* must re-sync with clients */

  /* Initialise list of city improvements. */
  for (i = 0; i < ARRAY_SIZE(pcity->built); i++) {
    pcity->built[i].turn = I_NEVER;
  }

  /* Load city improvements. */
  string = secfile_lookup_str(loading->file, "%s.improvements", citystr);
  sg_warn_ret_val(string != NULL, FALSE, "%s", secfile_error());
  sg_warn_ret_val(strlen(string) == loading->improvement.size, FALSE,
                  "Invalid length of '%s.improvements' (%lu ~= %lu).",
                  citystr, (unsigned long) strlen(string),
                  (unsigned long) loading->improvement.size);
  for (i = 0; i < loading->improvement.size; i++) {
    sg_warn_ret_val(string[i] == '1' || string[i] == '0', FALSE,
                   "Undefined value '%c' within '%s.improvements'.",
                   string[i], citystr)

    if (string[i] == '1') {
      struct impr_type *pimprove =
          improvement_by_rule_name(loading->improvement.order[i]);
      if (pimprove) {
        city_add_improvement(pcity, pimprove);
      }
    }
  }

  sg_failure_ret_val(loading->worked_tiles != NULL, FALSE,
                     "No worked tiles map defined.");

  city_freeze_workers(pcity);

  /* load new savegame with variable (squared) city radius and worked
   * tiles map */

  int radius_sq
    = secfile_lookup_int_default(loading->file, -1, "%s.city_radius_sq",
                                 citystr);
  city_map_radius_sq_set(pcity, radius_sq);

  city_tile_iterate(radius_sq, city_tile(pcity), ptile) {
    if (loading->worked_tiles[ptile->index] == pcity->id) {
      tile_set_worked(ptile, pcity);
      workers++;

#ifdef DEBUG
      /* set this tile to unused; a check for not resetted tiles is
       * included in game_load_internal() */
      loading->worked_tiles[ptile->index] = -1;
#endif /* DEBUG */
    }
  } city_tile_iterate_end;

  if (tile_worked(city_tile(pcity)) != pcity) {
    struct city *pwork = tile_worked(city_tile(pcity));

    if (NULL != pwork) {
      log_sg("[%s] city center of '%s' (%d,%d) [%d] is worked by '%s' "
             "(%d,%d) [%d]; repairing ", citystr, city_name(pcity),
             TILE_XY(city_tile(pcity)), city_size_get(pcity), city_name(pwork),
             TILE_XY(city_tile(pwork)), city_size_get(pwork));

      tile_set_worked(city_tile(pcity), NULL); /* remove tile from pwork */
      pwork->specialists[DEFAULT_SPECIALIST]++;
      auto_arrange_workers(pwork);
    } else {
      log_sg("[%s] city center of '%s' (%d,%d) [%d] is empty; repairing ",
             citystr, city_name(pcity), TILE_XY(city_tile(pcity)),
             city_size_get(pcity));
    }

    /* repair pcity */
    tile_set_worked(city_tile(pcity), pcity);
    city_repair_size(pcity, -1);
  }

  repair = city_size_get(pcity) - specialists - (workers - FREE_WORKED_TILES);
  if (0 != repair) {
    log_sg("[%s] size mismatch for '%s' (%d,%d): size [%d] != "
           "(workers [%d] - free worked tiles [%d]) + specialists [%d]",
           citystr, city_name(pcity), TILE_XY(city_tile(pcity)), city_size_get(pcity),
           workers, FREE_WORKED_TILES, specialists);

    /* repair pcity */
    city_repair_size(pcity, repair);
  }

  /* worklist_init() done in create_city_virtual() */
  worklist_load(loading->file, &pcity->worklist, "%s", citystr);

  /* Load city options. */
  BV_CLR_ALL(pcity->city_options);
  for (i = 0; i < CITYO_LAST; i++) {
    if (secfile_lookup_bool_default(loading->file, FALSE, "%s.option%d",
                                    citystr, i)) {
      BV_SET(pcity->city_options, i);
    }
  }

  CALL_FUNC_EACH_AI(city_load, loading->file, pcity, citystr);

  return TRUE;
}

/****************************************************************************
  Load nationality data for one city.
****************************************************************************/
static void sg_load_player_city_citizens(struct loaddata *loading,
                                         struct player *plr,
                                         struct city *pcity,
                                         const char *citystr)
{
  if (game.info.citizen_nationality == TRUE) {
    citizens size;

    citizens_init(pcity);
    player_slots_iterate(pslot) {
      int nationality;

      nationality = secfile_lookup_int_default(loading->file, -1,
                                               "%s.citizen%d", citystr,
                                               player_slot_index(pslot));
      if (nationality > 0 && !player_slot_is_used(pslot)) {
        log_sg("Citizens of an invalid nation for %s (player slot %d)!",
                city_name(pcity), player_slot_index(pslot));
        continue;
      }

      if (nationality != -1 && player_slot_is_used(pslot)) {
        sg_warn(nationality >= 0 && nationality <= MAX_CITY_SIZE,
                "Invalid value for citizens of player %d in %s: %d.",
                player_slot_index(pslot), city_name(pcity), nationality);
        citizens_nation_set(pcity, pslot, nationality);
      }
    } player_slots_iterate_end;
    /* Sanity check. */
    size = citizens_count(pcity);
    if (size != city_size_get(pcity)) {
      log_sg("City size and number of citizens does not match in %s "
             "(%d != %d)! Repairing ...", city_name(pcity),
             city_size_get(pcity), size);
      citizens_update(pcity);
    }
  }
}

/****************************************************************************
  Save cities data
****************************************************************************/
static void sg_save_player_cities(struct savedata *saving,
                                  struct player *plr)
{
  int wlist_max_length = 0;
  int i = 0;
  int plrno = player_number(plr);
  bool nations[MAX_NUM_PLAYER_SLOTS];

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  secfile_insert_int(saving->file, city_list_size(plr->cities),
                     "player%d.ncities", plrno);

  if (game.info.citizen_nationality == TRUE) {
    /* Initialise the nation list for the citizens information. */
    player_slots_iterate(pslot) {
      nations[player_slot_index(pslot)] = FALSE;
    } player_slots_iterate_end;
  }

  /* First determine lenght of longest worklist and the nations we have. */
  city_list_iterate(plr->cities, pcity) {
    /* Check the sanity of the city. */
    city_refresh(pcity);
    sanity_check_city(pcity);

    if (pcity->worklist.length > wlist_max_length) {
      wlist_max_length = pcity->worklist.length;
    }

    if (game.info.citizen_nationality == TRUE) {
      /* Find all nations of the citizens,*/
      players_iterate(pplayer) {
        if (!nations[player_index(pplayer)]
            && citizens_nation_get(pcity, pplayer->slot) != 0) {
          nations[player_index(pplayer)] = TRUE;
        }
      } players_iterate_end;
    }
  } city_list_iterate_end;

  city_list_iterate(plr->cities, pcity) {
    struct tile *pcenter = city_tile(pcity);
    char impr_buf[MAX_NUM_ITEMS + 1];
    char buf[32];
    int j, nat_x, nat_y;

    fc_snprintf(buf, sizeof(buf), "player%d.c%d", plrno, i);


    index_to_native_pos(&nat_x, &nat_y, tile_index(pcenter));
    secfile_insert_int(saving->file, nat_y, "%s.y", buf);
    secfile_insert_int(saving->file, nat_x, "%s.x", buf);

    secfile_insert_int(saving->file, pcity->id, "%s.id", buf);

    secfile_insert_int(saving->file, player_number(pcity->original),
                       "%s.original", buf);
    secfile_insert_int(saving->file, city_size_get(pcity), "%s.size", buf);

    specialist_type_iterate(sp) {
      secfile_insert_int(saving->file, pcity->specialists[sp], "%s.n%s", buf,
                         specialist_rule_name(specialist_by_number(sp)));
    } specialist_type_iterate_end;

    for (j = 0; j < MAX_TRADE_ROUTES; j++) {
      secfile_insert_int(saving->file, pcity->trade[j], "%s.traderoute%d",
                         buf, j);
    }

    secfile_insert_int(saving->file, pcity->food_stock, "%s.food_stock",
                       buf);
    secfile_insert_int(saving->file, pcity->shield_stock, "%s.shield_stock",
                       buf);

    secfile_insert_int(saving->file, pcity->airlift, "%s.airlift",
                       buf);
    secfile_insert_bool(saving->file, pcity->was_happy, "%s.was_happy",
                        buf);
    secfile_insert_int(saving->file, pcity->turn_plague, "%s.turn_plague",
                       buf);

    secfile_insert_int(saving->file, pcity->anarchy, "%s.anarchy", buf);
    secfile_insert_int(saving->file, pcity->rapture, "%s.rapture", buf);
    secfile_insert_int(saving->file, pcity->server.steal, "%s.steal", buf);

    secfile_insert_int(saving->file, pcity->turn_founded, "%s.turn_founded",
                       buf);
    if (pcity->turn_founded == game.info.turn) {
      j = -1; /* undocumented hack */
    } else {
      fc_assert(pcity->did_buy == TRUE || pcity->did_buy == FALSE);
      j = pcity->did_buy ? 1 : 0;
    }
    secfile_insert_int(saving->file, j, "%s.did_buy", buf);
    secfile_insert_bool(saving->file, pcity->did_sell, "%s.did_sell", buf);
    secfile_insert_int(saving->file, pcity->turn_last_built,
                       "%s.turn_last_built", buf);

    /* for visual debugging, variable length strings together here */
    secfile_insert_str(saving->file, city_name(pcity), "%s.name", buf);

    secfile_insert_str(saving->file, universal_type_rule_name(&pcity->production),
                       "%s.currently_building_kind", buf);
    secfile_insert_str(saving->file, universal_rule_name(&pcity->production),
                       "%s.currently_building_name", buf);

    secfile_insert_str(saving->file, universal_type_rule_name(&pcity->changed_from),
                       "%s.changed_from_kind", buf);
    secfile_insert_str(saving->file, universal_rule_name(&pcity->changed_from),
                       "%s.changed_from_name", buf);

    secfile_insert_int(saving->file, pcity->before_change_shields,
                       "%s.before_change_shields", buf);
    secfile_insert_int(saving->file, pcity->caravan_shields,
                       "%s.caravan_shields", buf);
    secfile_insert_int(saving->file, pcity->disbanded_shields,
                       "%s.disbanded_shields", buf);
    secfile_insert_int(saving->file, pcity->last_turns_shield_surplus,
                       "%s.last_turns_shield_surplus", buf);

    /* Save the squared city radius and all tiles within the corresponing
     * city map. */
    secfile_insert_int(saving->file, pcity->city_radius_sq,
                       "player%d.c%d.city_radius_sq", plrno, i);
    /* The tiles worked by the city are saved using the main map.
     * See also sg_save_map_worked(). */

    /* Save improvement list as bytevector. Note that improvement order
     * is saved in savefile.improvement_order. */
    improvement_iterate(pimprove) {
      impr_buf[improvement_index(pimprove)]
        = (pcity->built[improvement_index(pimprove)].turn <= I_NEVER) ? '0'
                                                                      : '1';
    } improvement_iterate_end;
    impr_buf[improvement_count()] = '\0';
    sg_failure_ret(strlen(impr_buf) < sizeof(impr_buf),
                   "Invalid size of the improvement vector (%s.improvements: "
                   "%lu < %lu).", buf, (long unsigned int) strlen(impr_buf),
                   (long unsigned int) sizeof(impr_buf));
    secfile_insert_str(saving->file, impr_buf, "%s.improvements", buf);

    worklist_save(saving->file, &pcity->worklist, wlist_max_length, "%s",
                  buf);

    for (j = 0; j < CITYO_LAST; j++) {
      secfile_insert_bool(saving->file, BV_ISSET(pcity->city_options, j),
                          "%s.option%d", buf, j);
    }

    CALL_FUNC_EACH_AI(city_save, saving->file, pcity, buf);

    if (game.info.citizen_nationality == TRUE) {
      /* Save nationality of the citizens,*/
      players_iterate(pplayer) {
        if (nations[player_index(pplayer)]) {
          secfile_insert_int(saving->file,
                             citizens_nation_get(pcity, pplayer->slot),
                             "%s.citizen%d", buf, player_index(pplayer));
        }
      } players_iterate_end;
    }

    i++;
  } city_list_iterate_end;
}

/****************************************************************************
  Load unit data
****************************************************************************/
static void sg_load_player_units(struct loaddata *loading,
                                 struct player *plr)
{
  int nunits, i, plrno = player_number(plr);

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  sg_failure_ret(secfile_lookup_int(loading->file, &nunits,
                                    "player%d.nunits", plrno),
                 "%s", secfile_error());
  if (!plr->is_alive && nunits > 0) {
    log_sg("'player%d.nunits' = %d for dead player!", plrno, nunits);
    nunits = 0; /* Some old savegames may be buggy. */
  }

  for (i = 0; i < nunits; i++) {
    struct unit *punit;
    struct city *pcity;
    const char *name;
    char buf[32];
    struct unit_type *type;

    fc_snprintf(buf, sizeof(buf), "player%d.u%d", plrno, i);

    name = secfile_lookup_str(loading->file, "%s.type_by_name", buf);
    type = unit_type_by_rule_name(name);
    sg_failure_ret(type != NULL, "%s: unknown unit type \"%s\".", buf, name);

    /* Create a dummy unit. */
    punit = unit_virtual_create(plr, NULL, type, 0);
    if (!sg_load_player_unit(loading, plr, punit, buf)) {
      unit_virtual_destroy(punit);
      sg_failure_ret(FALSE, "Error loading unit %d of player %d.", i, plrno);
    }

    identity_number_reserve(punit->id);
    idex_register_unit(punit);

    if ((pcity = game_city_by_number(punit->homecity))) {
      unit_list_prepend(pcity->units_supported, punit);
    } else if (punit->homecity > IDENTITY_NUMBER_ZERO) {
      log_sg("%s: bad home city %d.", buf, punit->homecity);
      punit->homecity = IDENTITY_NUMBER_ZERO;
    }

    /* allocate the unit's contribution to fog of war */
    punit->server.vision = vision_new(unit_owner(punit), unit_tile(punit));
    unit_refresh_vision(punit);
    /* NOTE: There used to be some map_set_known calls here.  These were
     * unneeded since unfogging the tile when the unit sees it will
     * automatically reveal that tile. */

    unit_list_append(plr->units, punit);
    unit_list_prepend(unit_tile(punit)->units, punit);

    /* Claim ownership of fortress? */
    if (tile_has_claimable_base(unit_tile(punit), unit_type(punit))
        && (!tile_owner(unit_tile(punit))
            || (tile_owner(unit_tile(punit))
                && pplayers_at_war(tile_owner(unit_tile(punit)), plr)))) {
      map_claim_ownership(unit_tile(punit), plr, unit_tile(punit));
      map_claim_border(unit_tile(punit), plr);
      /* city_thaw_workers_queue() later */
      /* city_refresh() later */
    }
  }
}

/****************************************************************************
  Load one unit. sg_save_player_unit() is not defined.
****************************************************************************/
static bool sg_load_player_unit(struct loaddata *loading,
                                struct player *plr, struct unit *punit,
                                const char *unitstr)
{
  int j;
  enum unit_activity activity;
  int nat_x, nat_y;
  enum tile_special_type target;
  struct base_type *pbase = NULL;
  struct road_type *proad = NULL;
  struct tile *ptile;
  int base;
  int road;
  int ei;
  const char *facing_str;
  enum tile_special_type cfspe;

  sg_warn_ret_val(secfile_lookup_int(loading->file, &punit->id, "%s.id",
                                     unitstr), FALSE, "%s", secfile_error());
  sg_warn_ret_val(secfile_lookup_int(loading->file, &nat_x, "%s.x", unitstr),
                  FALSE, "%s", secfile_error());
  sg_warn_ret_val(secfile_lookup_int(loading->file, &nat_y, "%s.y", unitstr),
                  FALSE, "%s", secfile_error());

  ptile = native_pos_to_tile(nat_x, nat_y);
  sg_warn_ret_val(NULL != ptile, FALSE, "%s invalid tile (%d, %d)",
                  unitstr, nat_x, nat_y);
  unit_tile_set(punit, ptile);

  facing_str
    = secfile_lookup_str_default(loading->file, "x",
                                 "%s.facing", unitstr);
  if (facing_str[0] != 'x') {
    /* We don't touch punit->facing if savegame does not contain that
     * information. Initial orientation set by unit_virtual_create()
     * is as good as any. */
    enum direction8 facing = char2dir(facing_str[0]);

    if (direction8_is_valid(facing)) {
      punit->facing = facing;
    } else {
      log_error("Illegal unit orientation '%s'", facing_str);
    }
  }

  sg_warn_ret_val(secfile_lookup_int(loading->file, &punit->homecity,
                                     "%s.homecity", unitstr), FALSE,
                  "%s", secfile_error());
  sg_warn_ret_val(secfile_lookup_int(loading->file, &punit->moves_left,
                                     "%s.moves", unitstr), FALSE,
                  "%s", secfile_error());
  sg_warn_ret_val(secfile_lookup_int(loading->file, &punit->fuel,
                                     "%s.fuel", unitstr), FALSE,
                  "%s", secfile_error());
  sg_warn_ret_val(secfile_lookup_int(loading->file, &ei,
                                     "%s.activity", unitstr), FALSE,
                  "%s", secfile_error());
  activity = ei;

  punit->server.birth_turn
    = secfile_lookup_int_default(loading->file, game.info.turn,
                                 "%s.born", unitstr);
  base = secfile_lookup_int_default(loading->file, -1,
                                    "%s.activity_base", unitstr);
  if (base >= 0 && base < loading->base.size) {
    pbase = loading->base.order[base];
  }
  road = secfile_lookup_int_default(loading->file, -1,
                                    "%s.activity_road", unitstr);
  if (road >= 0 && road < loading->road.size) {
    proad = loading->road.order[road];
  }

  {
    int tgt_no = secfile_lookup_int_default(loading->file,
                                            loading->special.size /* S_LAST */,
                                            "%s.activity_target", unitstr);
    if (tgt_no >= 0 && tgt_no < loading->special.size) {
      target = loading->special.order[tgt_no];
    } else {
      target = S_LAST;
    }
  }

  if (target == S_OLD_ROAD) {
    target = S_LAST;
    proad = road_by_special(S_ROAD);
  } else if (target == S_OLD_RAILROAD) {
    target = S_LAST;
    proad = road_by_special(S_RAILROAD);
  }

  if (activity == ACTIVITY_PATROL_UNUSED) {
    /* Previously ACTIVITY_PATROL and ACTIVITY_GOTO were used for
     * client-side goto. Now client-side goto is handled by setting
     * a special flag, and units with orders generally have ACTIVITY_IDLE.
     * Old orders are lost. Old client-side goto units will still have
     * ACTIVITY_GOTO and will goto the correct position via server goto.
     * Old client-side patrol units lose their patrol routes and are put
     * into idle mode. */
    activity = ACTIVITY_IDLE;
  }

  if (activity == ACTIVITY_FORTRESS) {
    activity = ACTIVITY_BASE;
    pbase = get_base_by_gui_type(BASE_GUI_FORTRESS, punit, unit_tile(punit));
  } else if (activity == ACTIVITY_AIRBASE) {
    activity = ACTIVITY_BASE;
    pbase = get_base_by_gui_type(BASE_GUI_AIRBASE, punit, unit_tile(punit));
  }

  if (activity == ACTIVITY_ROAD) {
    activity = ACTIVITY_GEN_ROAD;
    proad = road_by_special(S_ROAD);
  } else if (activity == ACTIVITY_RAILROAD) {
    activity = ACTIVITY_GEN_ROAD;
    proad = road_by_special(S_RAILROAD);
  }

  /* We need changed_from == ACTIVITY_IDLE by now so that
   * set_unit_activity() and friends don't spuriously restore activity
   * points -- unit should have been created this way */
  fc_assert(punit->changed_from == ACTIVITY_IDLE);

  if (activity == ACTIVITY_BASE) {
    if (pbase) {
      set_unit_activity_base(punit, base_number(pbase));
    } else {
      log_sg("Cannot find base %d for %s to build",
             base, unit_rule_name(punit));
      set_unit_activity(punit, ACTIVITY_IDLE);
    }
  } else if (activity == ACTIVITY_GEN_ROAD) {
    if (proad) {
      set_unit_activity_road(punit, road_number(proad));
    } else {
      log_sg("Cannot find road %d for %s to build",
             road, unit_rule_name(punit));
      set_unit_activity(punit, ACTIVITY_IDLE);
    }
  } else if (activity == ACTIVITY_PILLAGE) {
    struct act_tgt a_target;

    if (target != S_LAST) {
      a_target.type = ATT_SPECIAL;
      a_target.obj.spe = target;
    } else if (pbase != NULL) {
      a_target.type = ATT_BASE;
      a_target.obj.base = base_index(pbase);
    } else if (proad != NULL) {
      a_target.type = ATT_ROAD;
      a_target.obj.road = road_index(proad);
    } else {
      a_target.type = ATT_SPECIAL;
      a_target.obj.spe = S_LAST;
    }
    /* An out-of-range base number is seen with old savegames. We take
     * it as indicating undirected pillaging. We will assign pillage
     * targets before play starts. */
    set_unit_activity_targeted(punit, activity, &a_target);
  } else {
    set_unit_activity(punit, activity);
  }

  sg_warn_ret_val(secfile_lookup_int(loading->file, &punit->activity_count,
                                     "%s.activity_count", unitstr), FALSE,
                  "%s", secfile_error());

  punit->changed_from =
    secfile_lookup_int_default(loading->file, ACTIVITY_IDLE,
                               "%s.changed_from", unitstr);
  cfspe =
    secfile_lookup_int_default(loading->file, S_LAST,
                               "%s.changed_from_target", unitstr);
  base =
    secfile_lookup_int_default(loading->file, -1,
                               "%s.changed_from_base", unitstr);
  road =
    secfile_lookup_int_default(loading->file, -1,
                               "%s.changed_from_road", unitstr);

  if (road == -1) {
    if (cfspe == S_OLD_ROAD) {
      proad = road_type_by_rule_name("Road");
      if (proad) {
        road = road_index(proad);
      }
    } else if (cfspe == S_OLD_RAILROAD) {
      proad = road_by_special(S_RAILROAD);
      if (proad) {
        road = road_index(proad);
      }
    }
  }

  if (base >= 0 && base < loading->base.size) {
    punit->changed_from_target.type = ATT_BASE;
    punit->changed_from_target.obj.base = base_number(loading->base.order[base]);
  } else if (road >= 0 && road < loading->road.size) {
    punit->changed_from_target.type = ATT_ROAD;
    punit->changed_from_target.obj.road = road_number(loading->road.order[road]);
  } else {
    punit->changed_from_target.type = ATT_SPECIAL;
    punit->changed_from_target.obj.spe = cfspe;
  }
  punit->changed_from_count =
    secfile_lookup_int_default(loading->file, 0,
                               "%s.changed_from_count", unitstr);

  punit->veteran
    = secfile_lookup_int_default(loading->file, 0, "%s.veteran", unitstr);
  punit->done_moving
    = secfile_lookup_bool_default(loading->file, (punit->moves_left == 0),
                                  "%s.done_moving", unitstr);
  punit->battlegroup
    = secfile_lookup_int_default(loading->file, BATTLEGROUP_NONE,
                                 "%s.battlegroup", unitstr);

  if (secfile_lookup_bool_default(loading->file, FALSE,
                                  "%s.go", unitstr)) {
    int nat_x, nat_y;

    sg_warn_ret_val(secfile_lookup_int(loading->file, &nat_x,
                                       "%s.goto_x", unitstr), FALSE,
                    "%s", secfile_error());
    sg_warn_ret_val(secfile_lookup_int(loading->file, &nat_y,
                                       "%s.goto_y", unitstr), FALSE,
                    "%s", secfile_error());

    punit->goto_tile = native_pos_to_tile(nat_x, nat_y);
  } else {
    punit->goto_tile = NULL;

    /* This variables are not used but needed for saving the unit table.
     * Load them to prevent unused variables errors. */
    (void) secfile_lookup_int_default(loading->file, 0, "%s.goto_x",
                                      unitstr);
    (void) secfile_lookup_int_default(loading->file, 0, "%s.goto_y",
                                      unitstr);
  }

  /* Load AI data of the unit. */
  CALL_FUNC_EACH_AI(unit_load, loading->file, punit, unitstr);

  sg_warn_ret_val(secfile_lookup_bool(loading->file,
                                      &punit->ai_controlled,
                                      "%s.ai", unitstr), FALSE,
                  "%s", secfile_error());
  sg_warn_ret_val(secfile_lookup_int(loading->file, &punit->hp,
                                     "%s.hp", unitstr), FALSE,
                  "%s", secfile_error());

  punit->server.ord_map
    = secfile_lookup_int_default(loading->file, 0, "%s.ord_map", unitstr);
  punit->server.ord_city
    = secfile_lookup_int_default(loading->file, 0, "%s.ord_city", unitstr);
  punit->moved
    = secfile_lookup_bool_default(loading->file, FALSE, "%s.moved", unitstr);
  punit->paradropped
    = secfile_lookup_bool_default(loading->file, FALSE,
                                  "%s.paradropped", unitstr);

  /* The transport status (punit->transported_by) is loaded in
   * sg_player_units_transport(). */

  /* Initialize upkeep values: these are hopefully initialized
   * elsewhere before use (specifically, in city_support(); but
   * fixme: check whether always correctly initialized?).
   * Below is mainly for units which don't have homecity --
   * otherwise these don't get initialized (and AI calculations
   * etc may use junk values). */
  output_type_iterate(o) {
    punit->upkeep[o] = utype_upkeep_cost(unit_type(punit), plr, o);
  } output_type_iterate_end;

  /* load the unit orders */
  {
    int len = secfile_lookup_int_default(loading->file, 0,
                                         "%s.orders_length", unitstr);
    if (len > 0) {
      const char *orders_unitstr, *dir_unitstr, *act_unitstr, *base_unitstr;
      const char *road_unitstr;
      int road_idx = road_index(road_by_special(S_ROAD));
      int rail_idx = road_index(road_by_special(S_RAILROAD));

      punit->orders.list = fc_malloc(len * sizeof(*(punit->orders.list)));
      punit->orders.length = len;
      punit->orders.index
        = secfile_lookup_int_default(loading->file, 0,
                                     "%s.orders_index", unitstr);
      punit->orders.repeat
        = secfile_lookup_bool_default(loading->file, FALSE,
                                      "%s.orders_repeat", unitstr);
      punit->orders.vigilant
        = secfile_lookup_bool_default(loading->file, FALSE,
                                      "%s.orders_vigilant", unitstr);

      orders_unitstr
        = secfile_lookup_str_default(loading->file, "",
                                     "%s.orders_list", unitstr);
      dir_unitstr
        = secfile_lookup_str_default(loading->file, "",
                                     "%s.dir_list", unitstr);
      act_unitstr
        = secfile_lookup_str_default(loading->file, "",
                                     "%s.activity_list", unitstr);
      base_unitstr
        = secfile_lookup_str(loading->file, "%s.base_list", unitstr);
      road_unitstr
        = secfile_lookup_str_default(loading->file, NULL, "%s.road_list", unitstr);

      punit->has_orders = TRUE;
      for (j = 0; j < len; j++) {
        struct unit_order *order = &punit->orders.list[j];

        if (orders_unitstr[j] == '\0' || dir_unitstr[j] == '\0'
            || act_unitstr[j] == '\0') {
          log_sg("Invalid unit orders.");
          free_unit_orders(punit);
          break;
        }
        order->order = char2order(orders_unitstr[j]);
        order->dir = char2dir(dir_unitstr[j]);
        order->activity = char2activity(act_unitstr[j]);
        if (order->order == ORDER_LAST
            || (order->order == ORDER_MOVE && !direction8_is_valid(order->dir))
            || (order->order == ORDER_ACTIVITY
                && order->activity == ACTIVITY_LAST)) {
          /* An invalid order. Just drop the orders for this unit. */
          free(punit->orders.list);
          punit->orders.list = NULL;
          punit->has_orders = FALSE;
          break;
        }

        if (base_unitstr && base_unitstr[j] != '?') {
          base = char2num(base_unitstr[j]);

          if (base < 0 || base >= loading->base.size) {
            log_sg("Cannot find base %d for %s to build",
                   base, unit_rule_name(punit));
            base = base_number(get_base_by_gui_type(BASE_GUI_FORTRESS,
                                                    NULL, NULL));
          }

          order->base = base;
        }

        if (road_unitstr && road_unitstr[j] != '?') {
          road = char2num(road_unitstr[j]);

          if (road < 0 || road >= loading->road.size) {
            log_sg("Cannot find road %d for %s to build",
                   road, unit_rule_name(punit));
            road = 0;
          }

          order->road = road;
        }

        if (order->activity == ACTIVITY_ROAD) {
          order->activity = ACTIVITY_GEN_ROAD;
          order->road = road_idx;
        } else if (order->activity == ACTIVITY_RAILROAD) {
          order->activity = ACTIVITY_GEN_ROAD;
          order->road = rail_idx;
        }
      }
    } else {
      punit->has_orders = FALSE;
      punit->orders.list = NULL;
    }
  }

  return TRUE;
}

/*****************************************************************************
  Load the transport status of all units. This is seperated from the other
  code as all units must be known.
*****************************************************************************/
static void sg_load_player_units_transport(struct loaddata *loading,
                                           struct player *plr)
{
  int nunits, i, plrno = player_number(plr);

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* Recheck the number of units for the player. This is a copied from
   * sg_load_player_units(). */
  sg_failure_ret(secfile_lookup_int(loading->file, &nunits,
                                    "player%d.nunits", plrno),
                 "%s", secfile_error());
  if (!plr->is_alive && nunits > 0) {
    log_sg("'player%d.nunits' = %d for dead player!", plrno, nunits);
    nunits = 0; /* Some old savegames may be buggy. */
  }

  for (i = 0; i < nunits; i++) {
    int id_unit, id_trans;
    struct unit *punit, *ptrans;

    id_unit = secfile_lookup_int_default(loading->file, -1,
                                         "player%d.u%d.id",
                                         plrno, i);
    punit = player_unit_by_number(plr, id_unit);
    fc_assert_action(punit != NULL, continue);

    id_trans = secfile_lookup_int_default(loading->file, -1,
                                          "player%d.u%d.transported_by",
                                          plrno, i);
    if (id_trans == -1) {
      /* Not transported. */
      continue;
    }

    ptrans = game_unit_by_number(id_trans);
    fc_assert_action(id_trans == -1 || ptrans != NULL, continue);

    if (ptrans) {
      fc_assert_action(unit_transport_load(punit, ptrans, TRUE), continue);
    }
  }
}

/****************************************************************************
  Save unit data
****************************************************************************/
static void sg_save_player_units(struct savedata *saving,
                                 struct player *plr)
{
  int i = 0;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  secfile_insert_int(saving->file, unit_list_size(plr->units),
                     "player%d.nunits", player_number(plr));

  unit_list_iterate(plr->units, punit) {
    char buf[32];
    char dirbuf[2] = " ";
    int nat_x, nat_y;

    fc_snprintf(buf, sizeof(buf), "player%d.u%d", player_number(plr), i);
    dirbuf[0] = dir2char(punit->facing);
    secfile_insert_int(saving->file, punit->id, "%s.id", buf);

    index_to_native_pos(&nat_x, &nat_y, tile_index(unit_tile(punit)));
    secfile_insert_int(saving->file, nat_x, "%s.x", buf);
    secfile_insert_int(saving->file, nat_y, "%s.y", buf);

    secfile_insert_str(saving->file, dirbuf, "%s.facing", buf);
    secfile_insert_int(saving->file, punit->veteran, "%s.veteran", buf);
    secfile_insert_int(saving->file, punit->hp, "%s.hp", buf);
    secfile_insert_int(saving->file, punit->homecity, "%s.homecity", buf);
    secfile_insert_str(saving->file, unit_rule_name(punit),
                       "%s.type_by_name", buf);

    secfile_insert_int(saving->file, punit->activity, "%s.activity", buf);
    secfile_insert_int(saving->file, punit->activity_count,
                       "%s.activity_count", buf);
    if (punit->activity_target.type == ATT_SPECIAL) {
      secfile_insert_int(saving->file, punit->activity_target.obj.spe,
                         "%s.activity_target", buf);
    } else {
      secfile_insert_int(saving->file, S_LAST,
                         "%s.activity_target", buf);
    }
    if (punit->activity_target.type == ATT_BASE) {
      secfile_insert_int(saving->file, punit->activity_target.obj.base,
                         "%s.activity_base", buf);
    } else {
      secfile_insert_int(saving->file, BASE_NONE,
                         "%s.activity_base", buf);
    }
    if (punit->activity_target.type == ATT_ROAD) {
      secfile_insert_int(saving->file, punit->activity_target.obj.road,
                         "%s.activity_road", buf);
    } else {
      secfile_insert_int(saving->file, ROAD_NONE,
                         "%s.activity_road", buf);
    }
    secfile_insert_int(saving->file, punit->changed_from,
                       "%s.changed_from", buf);
    secfile_insert_int(saving->file, punit->changed_from_count,
                       "%s.changed_from_count", buf);
    if (punit->changed_from_target.type == ATT_SPECIAL) {
      secfile_insert_int(saving->file, punit->changed_from_target.obj.spe,
                         "%s.changed_from_target", buf);
    } else {
      secfile_insert_int(saving->file, S_LAST,
                         "%s.changed_from_target", buf);
    }
    if (punit->changed_from_target.type == ATT_BASE) {
      secfile_insert_int(saving->file, punit->changed_from_target.obj.base,
                         "%s.changed_from_base", buf);
    } else {
      secfile_insert_int(saving->file, BASE_NONE,
                         "%s.changed_from_base", buf);
    }
    if (punit->changed_from_target.type == ATT_ROAD) {
      secfile_insert_int(saving->file, punit->changed_from_target.obj.road,
                         "%s.changed_from_road", buf);
    } else {
      secfile_insert_int(saving->file, ROAD_NONE,
                         "%s.changed_from_road", buf);
    }
    secfile_insert_bool(saving->file, punit->done_moving,
                        "%s.done_moving", buf);
    secfile_insert_int(saving->file, punit->moves_left, "%s.moves", buf);
    secfile_insert_int(saving->file, punit->fuel, "%s.fuel", buf);
    secfile_insert_int(saving->file, punit->server.birth_turn,
                      "%s.born", buf);
    secfile_insert_int(saving->file, punit->battlegroup,
                       "%s.battlegroup", buf);

    if (punit->goto_tile) {
      index_to_native_pos(&nat_x, &nat_y, tile_index(punit->goto_tile));
      secfile_insert_bool(saving->file, TRUE, "%s.go", buf);
      secfile_insert_int(saving->file, nat_x, "%s.goto_x", buf);
      secfile_insert_int(saving->file, nat_y, "%s.goto_y", buf);
    } else {
      secfile_insert_bool(saving->file, FALSE, "%s.go", buf);
      /* Set this values to allow saving it as table. */
      secfile_insert_int(saving->file, 0, "%s.goto_x", buf);
      secfile_insert_int(saving->file, 0, "%s.goto_y", buf);
    }

    secfile_insert_bool(saving->file, punit->ai_controlled,
                        "%s.ai", buf);

    /* Save AI data of the unit. */
    CALL_FUNC_EACH_AI(unit_save, saving->file, punit, buf);

    secfile_insert_int(saving->file, punit->server.ord_map,
                       "%s.ord_map", buf);
    secfile_insert_int(saving->file, punit->server.ord_city,
                       "%s.ord_city", buf);
    secfile_insert_bool(saving->file, punit->moved, "%s.moved", buf);
    secfile_insert_bool(saving->file, punit->paradropped,
                        "%s.paradropped", buf);
    secfile_insert_int(saving->file, unit_transport_get(punit)
                                     ? unit_transport_get(punit)->id : -1,
                       "%s.transported_by", buf);

    if (punit->has_orders) {
      int len = punit->orders.length, j;
      char orders_buf[len + 1], dir_buf[len + 1];
      char act_buf[len + 1], base_buf[len + 1];
      char road_buf[len + 1];

      secfile_insert_int(saving->file, len, "%s.orders_length", buf);
      secfile_insert_int(saving->file, punit->orders.index,
                         "%s.orders_index", buf);
      secfile_insert_bool(saving->file, punit->orders.repeat,
                          "%s.orders_repeat", buf);
      secfile_insert_bool(saving->file, punit->orders.vigilant,
                          "%s.orders_vigilant", buf);

      for (j = 0; j < len; j++) {
        orders_buf[j] = order2char(punit->orders.list[j].order);
        dir_buf[j] = '?';
        act_buf[j] = '?';
        base_buf[j] = '?';
        road_buf[j] = '?';
        switch (punit->orders.list[j].order) {
        case ORDER_MOVE:
          dir_buf[j] = dir2char(punit->orders.list[j].dir);
          break;
        case ORDER_ACTIVITY:
          if (punit->orders.list[j].activity == ACTIVITY_BASE) {
            base_buf[j] = num2char(punit->orders.list[j].base);
          } else if (punit->orders.list[j].activity == ACTIVITY_GEN_ROAD) {
            road_buf[j] = num2char(punit->orders.list[j].road);
          }
          act_buf[j] = activity2char(punit->orders.list[j].activity);
          break;
        case ORDER_FULL_MP:
        case ORDER_BUILD_CITY:
        case ORDER_DISBAND:
        case ORDER_BUILD_WONDER:
        case ORDER_TRADE_ROUTE:
        case ORDER_HOMECITY:
        case ORDER_LAST:
          break;
        }
      }
      orders_buf[len] = dir_buf[len] = act_buf[len] = base_buf[len] = '\0';
      road_buf[len] = '\0';

      secfile_insert_str(saving->file, orders_buf, "%s.orders_list", buf);
      secfile_insert_str(saving->file, dir_buf, "%s.dir_list", buf);
      secfile_insert_str(saving->file, act_buf, "%s.activity_list", buf);
      secfile_insert_str(saving->file, base_buf, "%s.base_list", buf);
      secfile_insert_str(saving->file, road_buf, "%s.road_list", buf);
    } else {
      /* Put all the same fields into the savegame - otherwise the
       * registry code can't correctly use a tabular format and the
       * savegame will be bigger. */
      secfile_insert_int(saving->file, 0, "%s.orders_length", buf);
      secfile_insert_int(saving->file, 0, "%s.orders_index", buf);
      secfile_insert_bool(saving->file, FALSE, "%s.orders_repeat", buf);
      secfile_insert_bool(saving->file, FALSE, "%s.orders_vigilant", buf);
      secfile_insert_str(saving->file, "-", "%s.orders_list", buf);
      secfile_insert_str(saving->file, "-", "%s.dir_list", buf);
      secfile_insert_str(saving->file, "-", "%s.activity_list", buf);
      secfile_insert_str(saving->file, "-", "%s.base_list", buf);
      secfile_insert_str(saving->file, "-", "%s.road_list", buf);
    }

    i++;
  } unit_list_iterate_end;
}

/****************************************************************************
  Load player (client) attributes data
****************************************************************************/
static void sg_load_player_attributes(struct loaddata *loading,
                                      struct player *plr)
{
  int plrno = player_number(plr);

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* Toss any existing attribute_block (should not exist) */
  if (plr->attribute_block.data) {
    free(plr->attribute_block.data);
    plr->attribute_block.data = NULL;
  }

  /* This is a big heap of opaque data for the client, check everything! */
  plr->attribute_block.length = secfile_lookup_int_default(
      loading->file, 0, "player%d.attribute_v2_block_length", plrno);

  if (0 > plr->attribute_block.length) {
    log_sg("player%d.attribute_v2_block_length=%d too small", plrno,
           plr->attribute_block.length);
    plr->attribute_block.length = 0;
  } else if (MAX_ATTRIBUTE_BLOCK < plr->attribute_block.length) {
    log_sg("player%d.attribute_v2_block_length=%d too big (max %d)",
           plrno, plr->attribute_block.length, MAX_ATTRIBUTE_BLOCK);
    plr->attribute_block.length = 0;
  } else if (0 < plr->attribute_block.length) {
    int part_nr, parts;
    size_t actual_length;
    int quoted_length;
    char *quoted;

    sg_failure_ret(
        secfile_lookup_int(loading->file, &quoted_length,
                           "player%d.attribute_v2_block_length_quoted",
                           plrno), "%s", secfile_error());
    sg_failure_ret(
        secfile_lookup_int(loading->file, &parts,
                           "player%d.attribute_v2_block_parts", plrno),
        "%s", secfile_error());

    quoted = fc_malloc(quoted_length + 1);
    quoted[0] = '\0';
    plr->attribute_block.data = fc_malloc(plr->attribute_block.length);
    for (part_nr = 0; part_nr < parts; part_nr++) {
      const char *current =
          secfile_lookup_str(loading->file,
                             "player%d.attribute_v2_block_data.part%d",
                             plrno, part_nr);
      if (!current) {
        log_sg("attribute_v2_block_parts=%d actual=%d", parts, part_nr);
        break;
      }
      log_debug("attribute_v2_block_length_quoted=%lu have=%lu part=%lu",
                (unsigned long) quoted_length,
                (unsigned long) strlen(quoted),
                (unsigned long) strlen(current));
      fc_assert(strlen(quoted) + strlen(current) <= quoted_length);
      strcat(quoted, current);
    }
    fc_assert_msg(quoted_length == strlen(quoted),
                  "attribute_v2_block_length_quoted=%lu actual=%lu",
                  (unsigned long) quoted_length,
                  (unsigned long) strlen(quoted));

    actual_length =
        unquote_block(quoted,
                      plr->attribute_block.data,
                      plr->attribute_block.length);
    fc_assert(actual_length == plr->attribute_block.length);
    free(quoted);
  }
}

/****************************************************************************
  Save player (client) attributes data.
****************************************************************************/
static void sg_save_player_attributes(struct savedata *saving,
                                      struct player *plr)
{
  int plrno = player_number(plr);

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* This is a big heap of opaque data from the client.  Although the binary
   * format is not user editable, keep the lines short enough for debugging,
   * and hope that data compression will keep the file a reasonable size.
   * Note that the "quoted" format is a multiple of 3.
   */
#define PART_SIZE (3*256)
#define PART_ADJUST (3)
  if (plr->attribute_block.data) {
    char part[PART_SIZE + PART_ADJUST];
    int parts;
    int current_part_nr;
    char *quoted = quote_block(plr->attribute_block.data,
                               plr->attribute_block.length);
    char *quoted_at = strchr(quoted, ':');
    size_t bytes_left = strlen(quoted);
    size_t bytes_at_colon = 1 + (quoted_at - quoted);
    size_t bytes_adjust = bytes_at_colon % PART_ADJUST;

    secfile_insert_int(saving->file, plr->attribute_block.length,
                       "player%d.attribute_v2_block_length", plrno);
    secfile_insert_int(saving->file, bytes_left,
                       "player%d.attribute_v2_block_length_quoted", plrno);

    /* Try to wring some compression efficiencies out of the "quoted" format.
     * The first line has a variable length decimal, mis-aligning triples.
     */
    if ((bytes_left - bytes_adjust) > PART_SIZE) {
      /* first line can be longer */
      parts = 1 + (bytes_left - bytes_adjust - 1) / PART_SIZE;
    } else {
      parts = 1;
    }

    secfile_insert_int(saving->file, parts,
                       "player%d.attribute_v2_block_parts", plrno);

    if (parts > 1) {
      size_t size_of_current_part = PART_SIZE + bytes_adjust;

      /* first line can be longer */
      memcpy(part, quoted, size_of_current_part);
      part[size_of_current_part] = '\0';
      secfile_insert_str(saving->file, part,
                         "player%d.attribute_v2_block_data.part%d",
                         plrno, 0);
      bytes_left -= size_of_current_part;
      quoted_at = &quoted[size_of_current_part];
      current_part_nr = 1;
    } else {
      quoted_at = quoted;
      current_part_nr = 0;
    }

    for (; current_part_nr < parts; current_part_nr++) {
      size_t size_of_current_part = MIN(bytes_left, PART_SIZE);

      memcpy(part, quoted_at, size_of_current_part);
      part[size_of_current_part] = '\0';
      secfile_insert_str(saving->file, part,
                         "player%d.attribute_v2_block_data.part%d",
                         plrno,
                         current_part_nr);
      bytes_left -= size_of_current_part;
      quoted_at = &quoted_at[size_of_current_part];
    }
    fc_assert(bytes_left == 0);
    free(quoted);
  }
#undef PART_ADJUST
#undef PART_SIZE
}

/****************************************************************************
  Load vision data
****************************************************************************/
static void sg_load_player_vision(struct loaddata *loading,
                                  struct player *plr)
{
  int plrno = player_number(plr);
  int total_ncities =
      secfile_lookup_int_default(loading->file, -1,
                                 "player%d.dc_total", plrno);
  int i;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (!plr->is_alive) {
    /* Reveal all for dead players. */
    map_know_and_see_all(plr);
  }

  if (!plr->is_alive
      || -1 == total_ncities
      || FALSE == game.info.fogofwar
      || !secfile_lookup_bool_default(loading->file, TRUE,
                                      "game.save_private_map")) {
    /* We have:
     * - a dead player;
     * - fogged cities are not saved for any reason;
     * - a savegame with fog of war turned off;
     * - or game.save_private_map is not set to FALSE in the scenario /
     * savegame. The players private knowledge is set to be what he could
     * see without fog of war. */
    whole_map_iterate(ptile) {
      if (map_is_known(ptile, plr)) {
        struct city *pcity = tile_city(ptile);

        update_player_tile_last_seen(plr, ptile);
        update_player_tile_knowledge(plr, ptile);

        if (NULL != pcity) {
          update_dumb_city(plr, pcity);
        }
      }
    } whole_map_iterate_end;

    /* Nothing more to do; */
    return;
  }

  /* Load player map (terrain). */
  LOAD_MAP_CHAR(ch, ptile,
                map_get_player_tile(ptile, plr)->terrain
                  = char2terrain(ch), loading->file,
                "player%d.map_t%04d", plrno);

  /* Load player map (resources). */
  LOAD_MAP_CHAR(ch, ptile,
                map_get_player_tile(ptile, plr)->resource
                  = char2resource(ch), loading->file,
                "player%d.map_res%04d", plrno);

  /* Load player map (specials). */
  halfbyte_iterate_special(j, loading->special.size) {
    LOAD_MAP_CHAR(ch, ptile,
                  sg_special_set(
                    &map_get_player_tile(ptile, plr)->special,
                    ch, loading->special.order + 4 * j, FALSE),
                  loading->file, "player%d.map_spe%02d_%04d", plrno, j);
  } halfbyte_iterate_special_end;

  /* Load player map (bases). */
  halfbyte_iterate_bases(j, loading->base.size) {
    LOAD_MAP_CHAR(ch, ptile,
                  sg_bases_set(&map_get_player_tile(ptile, plr)->bases,
                               ch, loading->base.order + 4 * j),
                  loading->file, "player%d.map_b%02d_%04d", plrno, j);
  } halfbyte_iterate_bases_end;

  /* Load player map (roads). */
  halfbyte_iterate_roads(j, loading->road.size) {
    LOAD_MAP_CHAR(ch, ptile,
                  sg_roads_set(&map_get_player_tile(ptile, plr)->roads,
                               ch, loading->road.order + 4 * j),
                  loading->file, "player%d.map_r%02d_%04d", plrno, j);
  } halfbyte_iterate_roads_end;

  if (game.server.foggedborders) {
    /* Load player map (border). */
    int x, y;

    for (y = 0; y < map.ysize; y++) {
      const char *buffer
        = secfile_lookup_str(loading->file, "player%d.map_owner%04d",
                             plrno, y);
      const char *ptr = buffer;

      sg_failure_ret(NULL != buffer,
                    "Savegame corrupt - map line %d not found.", y);
      for (x = 0; x < map.xsize; x++) {
        char token[TOKEN_SIZE];
        int number;
        struct tile *ptile = native_pos_to_tile(x, y);

        scanin(&ptr, ",", token, sizeof(token));
        sg_failure_ret('\0' != token[0],
                       "Savegame corrupt - map size not correct.");
        if (strcmp(token, "-") == 0) {
          map_get_player_tile(ptile, plr)->owner = NULL;
          continue;
        }

        sg_failure_ret(str_to_int(token, &number),
                       "Savegame corrupt - got tile owner=%s in (%d, %d).",
                       token, x, y);
        map_get_player_tile(ptile, plr)->owner = player_by_number(number);
      }
    }
  }

  /* Load player map (update time). */
  for (i = 0; i < 4; i++) {
    /* put 4-bit segments of 16-bit "updated" field */
    if (i == 0) {
      LOAD_MAP_CHAR(ch, ptile,
                    map_get_player_tile(ptile, plr)->last_updated
                      = ascii_hex2bin(ch, i),
                    loading->file, "player%d.map_u%02d_%04d", plrno, i);
    } else {
      LOAD_MAP_CHAR(ch, ptile,
                    map_get_player_tile(ptile, plr)->last_updated
                      |= ascii_hex2bin(ch, i),
                    loading->file, "player%d.map_u%02d_%04d", plrno, i);
    }
  }

  /* Load player map known cities. */
  for (i = 0; i < total_ncities; i++) {
    struct vision_site *pdcity;
    char buf[32];
    fc_snprintf(buf, sizeof(buf), "player%d.dc%d", plrno, i);

    pdcity = vision_site_new(0, NULL, NULL);
    if (sg_load_player_vision_city(loading, plr, pdcity, buf)) {
      change_playertile_site(map_get_player_tile(pdcity->location, plr),
                             pdcity);
      identity_number_reserve(pdcity->identity);
    } else {
      /* Error loading the data. */
      log_sg("Skipping seen city %d for player %d.", i, plrno);
      if (pdcity != NULL) {
        vision_site_destroy(pdcity);
      }
    }
  }

  /* Repair inconsistent player maps. */
  whole_map_iterate(ptile) {
    if (map_is_known_and_seen(ptile, plr, V_MAIN)) {
      struct city *pcity = tile_city(ptile);

      update_player_tile_knowledge(plr, ptile);
      reality_check_city(plr, ptile);

      if (NULL != pcity) {
        update_dumb_city(plr, pcity);
      }
    }
  } whole_map_iterate_end;
}

/****************************************************************************
  Load data for one seen city. sg_save_player_vision_city() is not defined.
****************************************************************************/
static bool sg_load_player_vision_city(struct loaddata *loading,
                                       struct player *plr,
                                       struct vision_site *pdcity,
                                       const char *citystr)
{
  const char *string;
  int i, id, size;
  citizens city_size;
  int nat_x, nat_y;

  sg_warn_ret_val(secfile_lookup_int(loading->file, &nat_x, "%s.x",
                                     citystr),
                  FALSE, "%s", secfile_error());
  sg_warn_ret_val(secfile_lookup_int(loading->file, &nat_y, "%s.y",
                                     citystr),
                  FALSE, "%s", secfile_error());
  pdcity->location = native_pos_to_tile(nat_x, nat_y);
  sg_warn_ret_val(NULL != pdcity->location, FALSE,
                  "%s invalid tile (%d,%d)", citystr, nat_x, nat_y);

  sg_warn_ret_val(secfile_lookup_int(loading->file, &id, "%s.owner",
                                     citystr),
                  FALSE, "%s", secfile_error());
  pdcity->owner = player_by_number(id);
  sg_warn_ret_val(NULL != pdcity->owner, FALSE,
                  "%s has invalid owner (%d); skipping.", citystr, id);

  sg_warn_ret_val(secfile_lookup_int(loading->file, &pdcity->identity,
                                     "%s.id", citystr),
                  FALSE, "%s", secfile_error());
  sg_warn_ret_val(IDENTITY_NUMBER_ZERO < pdcity->identity, FALSE,
                  "%s has invalid id (%d); skipping.", citystr, id);

  sg_warn_ret_val(secfile_lookup_int(loading->file, &size,
                                     "%s.size", citystr),
                  FALSE, "%s", secfile_error());
  city_size = (citizens)size; /* set the correct type */
  sg_warn_ret_val(size == (int)city_size, FALSE,
                  "Invalid city size: %d; set to %d.", size, city_size);
  vision_site_size_set(pdcity, city_size);

  /* Initialise list of improvements */
  BV_CLR_ALL(pdcity->improvements);
  string = secfile_lookup_str(loading->file, "%s.improvements", citystr);
  sg_warn_ret_val(string != NULL, FALSE, "%s", secfile_error());
  sg_warn_ret_val(strlen(string) == loading->improvement.size, FALSE,
                  "Invalid length of '%s.improvements' (%lu ~= %lu).",
                  citystr, (unsigned long) strlen(string),
                  (unsigned long) loading->improvement.size);
  for (i = 0; i < loading->improvement.size; i++) {
    sg_warn_ret_val(string[i] == '1' || string[i] == '0', FALSE,
                    "Undefined value '%c' within '%s.improvements'.",
                    string[i], citystr)

    if (string[i] == '1') {
      struct impr_type *pimprove =
          improvement_by_rule_name(loading->improvement.order[i]);
      if (pimprove) {
        BV_SET(pdcity->improvements, improvement_index(pimprove));
      }
    }
  }

  /* Use the section as backup name. */
  sz_strlcpy(pdcity->name, secfile_lookup_str_default(loading->file, citystr,
                                                      "%s.name", citystr));

  pdcity->occupied = secfile_lookup_bool_default(loading->file, FALSE,
                                                 "%s.occupied", citystr);
  pdcity->walls = secfile_lookup_bool_default(loading->file, FALSE,
                                              "%s.walls", citystr);
  pdcity->happy = secfile_lookup_bool_default(loading->file, FALSE,
                                              "%s.happy", citystr);
  pdcity->unhappy = secfile_lookup_bool_default(loading->file, FALSE,
                                                "%s.unhappy", citystr);

  return TRUE;
}

/****************************************************************************
  Save vision data
****************************************************************************/
static void sg_save_player_vision(struct savedata *saving,
                                  struct player *plr)
{
  int i, plrno = player_number(plr);

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (!game.info.fogofwar || !game.server.save_options.save_private_map) {
    /* The player can see all, there's no reason to save the private map. */
    return;
  }

  if (!plr->is_alive) {
    /* Nothing to save. */
    return;
  }

  /* Save the map (terrain). */
  SAVE_MAP_CHAR(ptile,
                terrain2char(map_get_player_tile(ptile, plr)->terrain),
                saving->file, "player%d.map_t%04d", plrno);

  /* Save the map (resources). */
  SAVE_MAP_CHAR(ptile,
                resource2char(map_get_player_tile(ptile, plr)->resource),
                saving->file, "player%d.map_res%04d", plrno);

  if (game.server.foggedborders) {
    /* Save the map (borders). */
    int x, y;

    for (y = 0; y < map.ysize; y++) {
      char line[map.xsize * TOKEN_SIZE];

      line[0] = '\0';
      for (x = 0; x < map.xsize; x++) {
        char token[TOKEN_SIZE];
        struct tile *ptile = native_pos_to_tile(x, y);
        struct player_tile *plrtile = map_get_player_tile(ptile, plr);

        if (plrtile == NULL || plrtile->owner == NULL) {
          strcpy(token, "-");
        } else {
          fc_snprintf(token, sizeof(token), "%d",
                      player_number(plrtile->owner));
        }
        strcat(line, token);
        if (x < map.xsize) {
          strcat(line, ",");
        }
      }
      secfile_insert_str(saving->file, line, "player%d.map_owner%04d",
                         plrno, y);
    }
  }

  /* Save the map (specials). */
  halfbyte_iterate_special(j, S_LAST) {
    enum tile_special_type mod[4];
    int l;

    for (l = 0; l < 4; l++) {
      mod[l] = MIN(4 * j + l, S_LAST);
    }
    SAVE_MAP_CHAR(ptile,
                  sg_special_get(map_get_player_tile(ptile, plr)->special,
                                 mod), saving->file,
                  "player%d.map_spe%02d_%04d", plrno, j);
  } halfbyte_iterate_special_end;

  /* Save the map (bases). */
  halfbyte_iterate_bases(j, game.control.num_base_types) {
    int mod[4];
    int l;

    for (l = 0; l < 4; l++) {
      if (4 * j + 1 > game.control.num_base_types) {
        mod[l] = -1;
      } else {
        mod[l] = 4 * j + l;
      }
    }

    SAVE_MAP_CHAR(ptile,
                  sg_bases_get(map_get_player_tile(ptile, plr)->bases, mod),
                  saving->file, "player%d.map_b%02d_%04d", plrno, j);
  } halfbyte_iterate_bases_end;

  /* Save the map (roads). */
  halfbyte_iterate_roads(j, game.control.num_road_types) {
    int mod[4];
    int l;

    for (l = 0; l < 4; l++) {
      if (4 * j + 1 > game.control.num_road_types) {
        mod[l] = -1;
      } else {
        mod[l] = 4 * j + l;
      }
    }

    SAVE_MAP_CHAR(ptile,
                  sg_roads_get(map_get_player_tile(ptile, plr)->roads, mod),
                  saving->file, "player%d.map_r%02d_%04d", plrno, j);
  } halfbyte_iterate_roads_end;

  /* Save the map (update time). */
  for (i = 0; i < 4; i++) {
    /* put 4-bit segments of 16-bit "updated" field */
    SAVE_MAP_CHAR(ptile,
                  bin2ascii_hex(
                    map_get_player_tile(ptile, plr)->last_updated, i),
                  saving->file, "player%d.map_u%02d_%04d", plrno, i);
  }

  /* Save known cities. */
  i = 0;
  whole_map_iterate(ptile) {
    struct vision_site *pdcity = map_get_player_city(ptile, plr);
    char impr_buf[MAX_NUM_ITEMS + 1];
    char buf[32];

    fc_snprintf(buf, sizeof(buf), "player%d.dc%d", plrno, i);

    if (NULL != pdcity && plr != vision_site_owner(pdcity)) {
      int nat_x, nat_y;

      index_to_native_pos(&nat_x, &nat_y, tile_index(ptile));
      secfile_insert_int(saving->file, nat_y, "%s.y", buf);
      secfile_insert_int(saving->file, nat_x, "%s.x", buf);

      secfile_insert_int(saving->file, pdcity->identity, "%s.id", buf);
      secfile_insert_int(saving->file, player_number(vision_site_owner(pdcity)),
                         "%s.owner", buf);

      secfile_insert_int(saving->file, vision_site_size_get(pdcity),
                         "%s.size", buf);
      secfile_insert_bool(saving->file, pdcity->occupied,
                          "%s.occupied", buf);
      secfile_insert_bool(saving->file, pdcity->walls, "%s.walls", buf);
      secfile_insert_bool(saving->file, pdcity->happy, "%s.happy", buf);
      secfile_insert_bool(saving->file, pdcity->unhappy, "%s.unhappy", buf);

      /* Save improvement list as bitvector. Note that improvement order
       * is saved in savefile.improvement.order. */
      improvement_iterate(pimprove) {
        impr_buf[improvement_index(pimprove)]
          = BV_ISSET(pdcity->improvements, improvement_index(pimprove))
            ? '1' : '0';
      } improvement_iterate_end;
      impr_buf[improvement_count()] = '\0';
      sg_failure_ret(strlen(impr_buf) < sizeof(impr_buf),
                     "Invalid size of the improvement vector (%s.improvements: "
                     "%lu < %lu).", buf, (long unsigned int) strlen(impr_buf),
                     (long unsigned int) sizeof(impr_buf));
      secfile_insert_str(saving->file, impr_buf, "%s.improvements", buf);
      secfile_insert_str(saving->file, pdcity->name, "%s.name", buf);

      i++;
    }
  } whole_map_iterate_end;

  secfile_insert_int(saving->file, i, "player%d.dc_total", plrno);
}

/* =======================================================================
 * Load / save the event cache. Sould be the last think to do.
 * ======================================================================= */

/****************************************************************************
  Load '[event_cache]'.
****************************************************************************/
static void sg_load_event_cache(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  event_cache_load(loading->file, "event_cache");
}

/****************************************************************************
  Save '[event_cache]'.
****************************************************************************/
static void sg_save_event_cache(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (saving->scenario) {
    /* Do _not_ save events in a scenario. */
    return;
  }

  event_cache_save(saving->file, "event_cache");
}

/* =======================================================================
 * Load / save the mapimg definitions.
 * ======================================================================= */

/****************************************************************************
  Load '[mapimg]'.
****************************************************************************/
static void sg_load_mapimg(struct loaddata *loading)
{
  int mapdef_count, i;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* Clear all defined map images. */
  while (mapimg_count() > 0) {
    mapimg_delete(0);
  }

  mapdef_count = secfile_lookup_int_default(loading->file, 0,
                                            "mapimg.count");
  log_verbose("Saved map image definitions: %d.", mapdef_count);

  if (0 >= mapdef_count) {
    return;
  }

  for (i = 0; i < mapdef_count; i++) {
    const char *p;

    p = secfile_lookup_str(loading->file, "mapimg.mapdef%d", i);
    if (NULL == p) {
      log_verbose("[Mapimg %4d] Missing definition.", i);
      continue;
    }

    if (!mapimg_define(p, FALSE)) {
      log_error("Invalid map image definition %4d: %s.", i, p);
    }

    log_verbose("Mapimg %4d loaded.", i);
  }
}

/****************************************************************************
  Save '[mapimg]'.
****************************************************************************/
static void sg_save_mapimg(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  secfile_insert_int(saving->file, mapimg_count(), "mapimg.count");
  if (mapimg_count() > 0) {
    int i;

    for (i = 0; i < mapimg_count(); i++) {
      char buf[MAX_LEN_MAPDEF];

      mapimg_id2str(i, buf, sizeof(buf));
      secfile_insert_str(saving->file, buf, "mapimg.mapdef%d", i);
    }
  }
}

/* =======================================================================
 * Sanity checks for loading / saving a game.
 * ======================================================================= */

/****************************************************************************
  Sanity check for loaded game.
****************************************************************************/
static void sg_load_sanitycheck(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (game.info.is_new_game) {
    /* Nothing to do for new games (or not started scenarios). */
    return;
  }

  /* Fix ferrying sanity */
  players_iterate(pplayer) {
    unit_list_iterate_safe(pplayer->units, punit) {
      if (!unit_transport_get(punit)
          && !can_unit_exist_at_tile(punit, unit_tile(punit))) {
        log_sg("Removing %s unferried %s in %s at (%d, %d)",
               nation_rule_name(nation_of_player(pplayer)),
               unit_rule_name(punit),
               terrain_rule_name(unit_tile(punit)->terrain),
               TILE_XY(unit_tile(punit)));
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

    /* Backward compatibility: if we had any open-ended orders (pillage)
     * in the savegame, assign specific targets now */
    unit_list_iterate(pplayer->units, punit) {
      unit_assign_specific_activity_target(punit,
                                           &punit->activity,
                                           &punit->activity_target);
    } unit_list_iterate_end;
  } players_iterate_end;

  /* Recalculate the potential buildings for each city. Has caused some
   * problems with game random state.
   * This also changes the game state if you save the game directly after
   * loading it and compare the results. */
  players_iterate(pplayer) {
    bool saved_ai_control = pplayer->ai_controlled;

    /* Recalculate for all players. */
    pplayer->ai_controlled = FALSE;

    /* Building advisor needs data phase open in order to work */
    adv_data_phase_init(pplayer, FALSE);
    building_advisor(pplayer);
    /* Close data phase again so it can be opened again when game starts. */
    adv_data_phase_done(pplayer);

    pplayer->ai_controlled = saved_ai_control;
  } players_iterate_end;

  /* Check worked tiles map */
#ifdef DEBUG
  if (loading->worked_tiles != NULL) {
    /* check the entire map for unused worked tiles */
    whole_map_iterate(ptile) {
      if (loading->worked_tiles[ptile->index] != -1) {
        log_error("[city id: %d] Unused worked tile at (%d, %d).",
                  loading->worked_tiles[ptile->index], TILE_XY(ptile));
      }
    } whole_map_iterate_end;
  }
#endif /* DEBUG */

  if (0 == strlen(server.game_identifier)
      || !is_base64url(server.game_identifier)) {
    /* This uses fc_rand(), so random state has to be initialized before. */
    randomize_base64url_string(server.game_identifier,
                               sizeof(server.game_identifier));
  }

  /* Restore game random state, just in case various initialization code
   * inexplicably altered the previously existing state. */
  if (!game.info.is_new_game) {
    fc_rand_set_state(loading->rstate);

    /* Recalculate scores. */
    players_iterate(pplayer) {
      calc_civ_score(pplayer);
    } players_iterate_end;
  }

  /* At the end do the default sanity checks. */
  sanity_check();
}

/****************************************************************************
  Sanity check for saved game.
****************************************************************************/
static void sg_save_sanitycheck(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();
}

/* =======================================================================
 * Compatibility functions for loading a game.
 * ======================================================================= */

/****************************************************************************
  Translate savegame secfile data from 2.3.x to 2.4.0 format.
****************************************************************************/
static void compat_load_020400(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  log_debug("Upgrading data from savegame to version 2.4.0");

  /* Add the default player AI. */
  player_slots_iterate(pslot) {
    int ncities, i, plrno = player_slot_index(pslot);

    if (NULL == secfile_section_lookup(loading->file, "player%d", plrno)) {
      continue;
    }

    secfile_insert_str(loading->file, default_ai_type_name(),
                       "player%d.ai_type", player_slot_index(pslot));

    /* Create dummy citizens informations. We do not know if citizens are
     * activated due to the fact that this information
     * (game.info.citizen_nationality) is not available, but adding the
     * information does no harm. */
    ncities = secfile_lookup_int_default(loading->file, 0,
                                         "player%d.ncities", plrno);
    if (ncities > 0) {
      for (i = 0; i < ncities; i++) {
        int size = secfile_lookup_int_default(loading->file, 0,
                                              "player%d.c%d.size", plrno, i);
        if (size > 0) {
          secfile_insert_int(loading->file, size,
                             "player%d.c%d.citizen%d", plrno, i, plrno);
        }
      }
    }

  } player_slots_iterate_end;

  /* Player colors are assigned automatically. */

  /* Deal with buggy known tiles information from 2.3.0/2.3.1 (and the
   * workaround in later 2.3.x); see gna bug #19029.
   * (The structure of this code is odd as it avoids relying on knowledge of
   * xsize/ysize, which haven't been extracted from the savefile yet.) */
  {
    if (has_capability("knownv2",
                       secfile_lookup_str(loading->file, "savefile.options"))) {
      /* This savefile contains known information in a sane format.
       * Just move any entries to where 2.4.x+ expect to find them. */
      struct section *map = secfile_section_by_name(loading->file, "map");
      if (map) {
        entry_list_iterate(section_entries(map), pentry) {
          const char *name = entry_name(pentry);
          if (strncmp(name, "kvb", 3) == 0) {
            /* Rename the "kvb..." entry to "k..." */
            char *name2 = fc_strdup(name), *newname = name2 + 2;
            *newname = 'k';
            /* Savefile probably contains existing "k" entries, which are bogus
             * so we trash them */
            secfile_entry_delete(loading->file, "map.%s", newname);
            entry_set_name(pentry, newname);
            FC_FREE(name2);
          }
        } entry_list_iterate_end;
      }
      /* Could remove "knownv2" from savefile.options, but it's doing
       * no harm there. */
    } else {
      /* This savefile only contains known information in the broken
       * format. Try to recover it to a sane format. */
      /* MAX_NUM_PLAYER_SLOTS in 2.3.x was 128 */
      /* MAP_MAX_LINEAR_SIZE in 2.3.x was 512 */
      const int maxslots = 128, maxmapsize = 512;
      const int lines = maxslots/32;
      int xsize = 0, y, l, j, x;
      unsigned int known_row_old[lines * maxmapsize],
                   known_row[lines * maxmapsize];
      /* Process a map row at a time */
      for (y = 0; y < maxmapsize; y++) {
        /* Look for broken info to convert */
        bool found = FALSE;
        memset(known_row_old, 0, sizeof(known_row_old));
        for (l = 0; l < lines; l++) {
          for (j = 0; j < 8; j++) {
            const char *s =
              secfile_lookup_str_default(loading->file, NULL,
                                         "map.k%02d_%04d", l * 8 + j, y);
            if (s) {
              found = TRUE;
              if (xsize == 0) {
                xsize = strlen(s);
              }
              sg_failure_ret(xsize == strlen(s),
                             "Inconsistent xsize in map.k%02d_%04d",
                             l * 8 + j, y);
              for (x = 0; x < xsize; x++) {
                known_row_old[l * xsize + x] |= ascii_hex2bin(s[x], j);
              }
            }
          }
        }
        if (found) {
          /* At least one entry found for this row. Let's hope they were
           * all there. */
          /* Attempt to munge into sane format */
          int p;
          memset(known_row, 0, sizeof(known_row));
          /* Iterate over possible player slots */
          for (p = 0; p < maxslots; p++) {
            l = p / 32;
            for (x = 0; x < xsize; x++) {
              /* This test causes bit-shifts of >=32 (undefined behaviour), but
               * on common platforms, information happens not to be lost, just
               * oddly arranged. */
              if (known_row_old[l * xsize + x] & (1u << (p - l * 8))) {
                known_row[l * xsize + x] |= (1u << (p - l * 32));
              }
            }
          }
          /* Save sane format back to memory representation of secfile for
           * real loading code to pick up */
          for (l = 0; l < lines; l++) {
            for (j = 0; j < 8; j++) {
              /* Save info for all slots (not just used ones). It's only
               * memory, after all. */
              char row[xsize+1];
              for (x = 0; x < xsize; x++) {
                row[x] = bin2ascii_hex(known_row[l * xsize + x], j);
              }
              row[xsize] = '\0';
              secfile_replace_str(loading->file, row,
                                  "map.k%02d_%04d", l * 8 + j, y);
            }
          }
        }
      }
    }
  }
}

/****************************************************************************
  Translate savegame secfile data from 2.4.x to 2.5.0 format.
****************************************************************************/
static void compat_load_020500(struct loaddata *loading)
{
  const char *modname[] = { "Road", "Railroad" };

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  log_debug("Upgrading data from savegame to version 2.5.0");

  secfile_insert_int(loading->file, 2, "savefile.roads_size");

  secfile_insert_str_vec(loading->file, modname, 2,
                         "savefile.roads_vector");
}

/****************************************************************************
  Compatibility functions for loaded game.

  This function is called at the beginning of loading a savegame. The data in
  loading->file should be change such, that the current loading functions can
  be executed without errors.
****************************************************************************/
static void sg_load_compat(struct loaddata *loading)
{
  int i, version;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  version = secfile_lookup_int_default(loading->file, -1,
                                       "savefile.version");
#ifdef DEBUG
  sg_failure_ret(0 < version, "Invalid savefile format version (%d).",
                 version);
  if (version > compat[compat_current].version) {
    /* Debug build can (TRY TO!) load newer versions but ... */
    log_error("Savegame version newer than this build found (%d > %d). "
              "Trying to load the game nevertheless ...", version,
              compat[compat_current].version);
  }
#else
  sg_failure_ret(0 < version && version <= compat[compat_current].version,
                 "Unknown savefile format version (%d).", version);
#endif /* DEBUG */


  for (i = 0; i < compat_num; i++) {
    if (version < compat[i].version && compat[i].load != NULL) {
      log_normal(_("Run compatibility function for version: <%d "
                   "(save file: %d; server: %d)."), compat[i].version,
                 version, compat[compat_current].version);
      compat[i].load(loading);
    }
  }
}
