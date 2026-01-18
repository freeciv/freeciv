/***********************************************************************
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
  3.0. It is defined by the mandatory option '+version3'. The main load
  function checks if this option is present. If not, the old (pre-3.0)
  loading routines are used.
  The format version is also saved in the settings section of the savefile, as an
  integer (savefile.version). The integer is used to determine the version
  of the savefile.

  Structure of this file:

  - The main function for saving is savegame3_save().

  - The real work is done by savegame3_load() and savegame3_save_real().
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
    'sg_success'. This variable is set to TRUE within savegame3_load().
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
#include "achievements.h"
#include "ai.h"
#include "bitvector.h"
#include "capability.h"
#include "citizens.h"
#include "city.h"
#include "counters.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "mapimg.h"
#include "movement.h"
#include "multipliers.h"
#include "packets.h"
#include "research.h"
#include "rgbcolor.h"
#include "sex.h"
#include "specialist.h"
#include "unit.h"
#include "unitlist.h"
#include "version.h"

/* server */
#include "barbarian.h"
#include "citizenshand.h"
#include "citytools.h"
#include "cityturn.h"
#include "diplhand.h"
#include "maphand.h"
#include "meta.h"
#include "notify.h"
#include "plrhand.h"
#include "report.h"
#include "ruleload.h"
#include "sanitycheck.h"
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
#include "mapgen_utils.h"

/* server/scripting */
#include "script_server.h"

/* server/savegame */
#include "savecompat.h"
#include "savemain.h"

/* ai */
#include "aitraits.h"
#include "difficulty.h"

#include "savegame3.h"

extern bool sg_success;

#define ACTIVITY_OLD_POLLUTION_SG3 (ACTIVITY_LAST + 1)
#define ACTIVITY_OLD_FALLOUT_SG3 (ACTIVITY_OLD_POLLUTION_SG3 + 1)
#define ACTIVITY_LAST_SAVEGAME3 (ACTIVITY_OLD_FALLOUT_SG3 + 1)

#ifdef FREECIV_TESTMATIC
#define SAVE_DUMMY_TURN_CHANGE_TIME 1
#endif

/*
 * This loops over the entire map to save data. It collects all the data of
 * a line using GET_XY_CHAR and then executes the macro SECFILE_INSERT_LINE.
 *
 * Parameters:
 *   ptile:         current tile within the line (used by GET_XY_CHAR)
 *   GET_XY_CHAR:   macro returning the map character for each position
 *   secfile:       a secfile struct
 *   secpath, ...:  path as used for sprintf() with arguments; the last item
 *                  will be the y coordinate
 * Example:
 *   SAVE_MAP_CHAR(ptile, terrain2char(ptile->terrain), file, "map.t%04d");
 */
#define SAVE_MAP_CHAR(ptile, GET_XY_CHAR, secfile, secpath, ...)            \
{                                                                           \
  char _line[MAP_NATIVE_WIDTH + 1];                                         \
  int _nat_x, _nat_y;                                                       \
                                                                            \
  for (_nat_y = 0; _nat_y < MAP_NATIVE_HEIGHT; _nat_y++) {                  \
    for (_nat_x = 0; _nat_x < MAP_NATIVE_WIDTH; _nat_x++) {                 \
      struct tile *ptile = native_pos_to_tile(&(wld.map), _nat_x, _nat_y);  \
      fc_assert_action(ptile != NULL, continue);                            \
      _line[_nat_x] = (GET_XY_CHAR);                                        \
      sg_failure_ret(fc_isprint(_line[_nat_x] & 0x7f),                      \
                     "Trying to write invalid map data at position "        \
                     "(%d, %d) for path %s: '%c' (%d)", _nat_x, _nat_y,     \
                     secpath, _line[_nat_x], _line[_nat_x]);                \
    }                                                                       \
    _line[MAP_NATIVE_WIDTH] = '\0';                                         \
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
 *                  will be the y coordinate
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
  for (_nat_y = 0; _nat_y < MAP_NATIVE_HEIGHT; _nat_y++) {                  \
    const char *_line = secfile_lookup_str(secfile, secpath,                \
                                           ## __VA_ARGS__, _nat_y);         \
    if (NULL == _line) {                                                    \
      char buf[64];                                                         \
      fc_snprintf(buf, sizeof(buf), secpath, ## __VA_ARGS__, _nat_y);       \
      log_verbose("Line not found='%s'", buf);                              \
      _printed_warning = TRUE;                                              \
      continue;                                                             \
    } else if (strlen(_line) != MAP_NATIVE_WIDTH) {                         \
      char buf[64];                                                         \
      fc_snprintf(buf, sizeof(buf), secpath, ## __VA_ARGS__, _nat_y);       \
      log_verbose("Line too short (expected %d got " SIZE_T_PRINTF          \
                  ")='%s'", MAP_NATIVE_WIDTH, strlen(_line), buf);          \
      _printed_warning = TRUE;                                              \
      continue;                                                             \
    }                                                                       \
    for (_nat_x = 0; _nat_x < MAP_NATIVE_WIDTH; _nat_x++) {                 \
      const char ch = _line[_nat_x];                                        \
      struct tile *ptile = native_pos_to_tile(&(wld.map), _nat_x, _nat_y);  \
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

/* Iterate on the extras half-bytes */
#define halfbyte_iterate_extras(e, num_extras_types)                        \
{                                                                           \
  int e;                                                                    \
  for (e = 0; 4 * e < (num_extras_types); e++) {

#define halfbyte_iterate_extras_end                                         \
  }                                                                         \
}

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

static const char savefile_options_default[] =
  " +version3";
/* The following savefile option are added if needed:
 *  - nothing at current version
 * See also calls to sg_save_savefile_options(). */

static void savegame3_save_real(struct section_file *file,
                                const char *save_reason,
                                bool scenario);
static struct loaddata *loaddata_new(struct section_file *file);
static void loaddata_destroy(struct loaddata *loading);

static struct savedata *savedata_new(struct section_file *file,
                                     const char *save_reason,
                                     bool scenario);
static void savedata_destroy(struct savedata *saving);

static enum unit_orders char2order(char order);
static char order2char(enum unit_orders order);
static enum direction8 char2dir(char dir);
static char dir2char(enum direction8 dir);
static char activity2char(int activity);
static enum unit_activity char2activity(char activity);
static char *quote_block(const void *const data, int length);
static int unquote_block(const char *const quoted_, void *dest,
                         int dest_length);
static void worklist_load(struct section_file *file, int wlist_max_length,
                          struct worklist *pwl, const char *path, ...);
static void worklist_save(struct section_file *file,
                          const struct worklist *pwl,
                          int max_length, const char *path, ...);
static void unit_ordering_calc(void);
static void unit_ordering_apply(void);
static void sg_extras_set_dbv(struct dbv *extras, char ch, struct extra_type **idx);
static void sg_extras_set_bv(bv_extras *extras, char ch, struct extra_type **idx);
static char sg_extras_get_dbv(struct dbv *extras, struct extra_type *presource,
                              const int *idx);
static char sg_extras_get_bv(bv_extras extras, struct extra_type *presource,
                             const int *idx);
static struct terrain *char2terrain(char ch);
static char terrain2char(const struct terrain *pterrain);
static Tech_type_id technology_load(struct section_file *file,
                                    const char *path, int plrno);
static void technology_save(struct section_file *file,
                            const char *path, int plrno, Tech_type_id tech);

static void sg_load_savefile(struct loaddata *loading);
static void sg_save_savefile(struct savedata *saving);
static void sg_save_savefile_options(struct savedata *saving,
                                     const char *option);

static void sg_load_game(struct loaddata *loading);
static void sg_save_game(struct savedata *saving);

static void sg_load_ruledata(struct loaddata *loading);
static void sg_save_ruledata(struct savedata *saving);

static void sg_load_random(struct loaddata *loading);
static void sg_save_random(struct savedata *saving);

static void sg_load_script(struct loaddata *loading);
static void sg_save_script(struct savedata *saving);

static void sg_load_scenario(struct loaddata *loading);
static void sg_save_scenario(struct savedata *saving);

static void sg_load_settings(struct loaddata *loading);
static void sg_save_settings(struct savedata *saving);

static void sg_load_counters (struct loaddata * loading);
static void sg_save_counters (struct savedata * saving);

static void sg_load_map(struct loaddata *loading);
static void sg_save_map(struct savedata *saving);
static void sg_load_map_tiles(struct loaddata *loading);
static void sg_load_map_altitude(struct loaddata *loading);
static void sg_save_map_tiles(struct savedata *saving);
static void sg_save_map_altitude(struct savedata *saving);
static void sg_load_map_tiles_extras(struct loaddata *loading);
static void sg_save_map_tiles_extras(struct savedata *saving);

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
                                struct city *pcity, const char *citystr,
                                int wlist_max_length, int routes_max);
static void sg_load_player_city_citizens(struct loaddata *loading,
                                         struct player *plr,
                                         struct city *pcity,
                                         const char *citystr);
static void sg_load_player_units(struct loaddata *loading,
                                 struct player *plr);
static bool sg_load_player_unit(struct loaddata *loading,
                                struct player *plr, struct unit *punit,
                                int orders_max_length,
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

static void sg_load_researches(struct loaddata *loading);
static void sg_save_researches(struct savedata *saving);

static void sg_load_event_cache(struct loaddata *loading);
static void sg_save_event_cache(struct savedata *saving);

static void sg_load_treaties(struct loaddata *loading);
static void sg_save_treaties(struct savedata *saving);

static void sg_load_history(struct loaddata *loading);
static void sg_save_history(struct savedata *saving);

static void sg_load_mapimg(struct loaddata *loading);
static void sg_save_mapimg(struct savedata *saving);

static void sg_load_sanitycheck(struct loaddata *loading);
static void sg_save_sanitycheck(struct savedata *saving);


/************************************************************************//**
  Main entry point for saving a game in savegame3 format.
****************************************************************************/
void savegame3_save(struct section_file *sfile, const char *save_reason,
                    bool scenario)
{
  fc_assert_ret(sfile != NULL);

#ifdef DEBUG_TIMERS
  struct timer *savetimer = timer_new(TIMER_CPU, TIMER_DEBUG, "save");
  timer_start(savetimer);
#endif

  log_verbose("saving game in new format ...");
  savegame3_save_real(sfile, save_reason, scenario);

#ifdef DEBUG_TIMERS
  timer_stop(savetimer);
  log_debug("Creating secfile in %.3f seconds.", timer_read_seconds(savetimer));
  timer_destroy(savetimer);
#endif /* DEBUG_TIMERS */
}

/* =======================================================================
 * Basic load / save functions.
 * ======================================================================= */

/************************************************************************//**
  Really loading the savegame.
****************************************************************************/
void savegame3_load(struct section_file *file)
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
  sg_load_compat(loading, SAVEGAME_3);
  /* [scenario] */
  sg_load_scenario(loading);
  /* [savefile] */
  sg_load_savefile(loading);
  /* [game] */
  sg_load_game(loading);
  /* [random] */
  sg_load_random(loading);
  /* [settings] */
  sg_load_settings(loading);
  /* [ruledata] */
  sg_load_ruledata(loading);
  /* [players] (basic data) */
  sg_load_players_basic(loading);
  /* [map]; needs width and height loaded by [settings]  */
  sg_load_map(loading);
  /* [research] */
  sg_load_researches(loading);
  /* [player<i>] */
  sg_load_players(loading);
  /* [counters] */
  sg_load_counters(loading);
  /* [event_cache] */
  sg_load_event_cache(loading);
  /* [treaties] */
  sg_load_treaties(loading);
  /* [history] */
  sg_load_history(loading);
  /* [mapimg] */
  sg_load_mapimg(loading);
  /* [script] -- must come last as may reference game objects */
  sg_load_script(loading);
  /* [post_load_compat]; needs the game loaded by [savefile] */
  sg_load_post_load_compat(loading, SAVEGAME_3);

  /* Sanity checks for the loaded game. */
  sg_load_sanitycheck(loading);

  /* deinitialise loading */
  loaddata_destroy(loading);
  send_tile_suppression(was_send_tile_suppressed);
  send_city_suppression(was_send_city_suppressed);

  if (!sg_success) {
    log_error("Failure loading savegame!");
    save_restore_sane_state();
  }
}

/************************************************************************//**
  Really save the game to a file.
****************************************************************************/
static void savegame3_save_real(struct section_file *file,
                                const char *save_reason,
                                bool scenario)
{
  struct savedata *saving;

  /* initialise loading */
  saving = savedata_new(file, save_reason, scenario);
  sg_success = TRUE;

  /* [scenario] */
  /* This should be first section so scanning through all scenarios just for
   * names and descriptions would go faster. */
  sg_save_scenario(saving);
  /* [savefile] */
  sg_save_savefile(saving);
  /* [counters] */
  sg_save_counters(saving);
  /* [game] */
  sg_save_game(saving);
  /* [random] */
  sg_save_random(saving);
  /* [script] */
  sg_save_script(saving);
  /* [settings] */
  sg_save_settings(saving);
  /* [ruledata] */
  sg_save_ruledata(saving);
  /* [map] */
  sg_save_map(saving);
  /* [player<i>] */
  sg_save_players(saving);
  /* [research] */
  sg_save_researches(saving);
  /* [event_cache] */
  sg_save_event_cache(saving);
  /* [treaty<i>] */
  sg_save_treaties(saving);
  /* [history] */
  sg_save_history(saving);
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

/************************************************************************//**
  Create new loaddata item for given section file.
****************************************************************************/
static struct loaddata *loaddata_new(struct section_file *file)
{
  struct loaddata *loading = calloc(1, sizeof(*loading));
  loading->file = file;
  loading->secfile_options = NULL;

  loading->improvement.order = NULL;
  loading->improvement.size = -1;
  loading->technology.order = NULL;
  loading->technology.size = -1;
  loading->activities.order = NULL;
  loading->activities.size = -1;
  loading->trait.order = NULL;
  loading->trait.size = -1;
  loading->extra.order = NULL;
  loading->extra.size = -1;
  loading->multiplier.order = NULL;
  loading->multiplier.size = -1;
  loading->specialist.order = NULL;
  loading->specialist.size = -1;
  loading->action.order = NULL;
  loading->action.size = -1;
  loading->act_dec.order = NULL;
  loading->act_dec.size = -1;
  loading->ssa.order = NULL;
  loading->ssa.size = -1;
  loading->coptions.order = NULL;
  loading->coptions.size = -1;

  loading->server_state = S_S_INITIAL;
  loading->rstate = fc_rand_state();
  loading->worked_tiles = NULL;

  return loading;
}

/************************************************************************//**
  Free resources allocated for loaddata item.
****************************************************************************/
static void loaddata_destroy(struct loaddata *loading)
{
  if (loading->improvement.order != NULL) {
    free(loading->improvement.order);
  }

  if (loading->technology.order != NULL) {
    free(loading->technology.order);
  }

  if (loading->activities.order != NULL) {
    free(loading->activities.order);
  }

  if (loading->trait.order != NULL) {
    free(loading->trait.order);
  }

  if (loading->extra.order != NULL) {
    free(loading->extra.order);
  }

  if (loading->multiplier.order != NULL) {
    free(loading->multiplier.order);
  }

  if (loading->specialist.order != NULL) {
    free(loading->specialist.order);
  }

  if (loading->action.order != NULL) {
    free(loading->action.order);
  }

  if (loading->act_dec.order != NULL) {
    free(loading->act_dec.order);
  }

  if (loading->ssa.order != NULL) {
    free(loading->ssa.order);
  }

  if (loading->coptions.order != NULL) {
    free(loading->coptions.order);
  }

  if (loading->worked_tiles != NULL) {
    free(loading->worked_tiles);
  }

  free(loading);
}

/************************************************************************//**
  Create new savedata item for given file.
****************************************************************************/
static struct savedata *savedata_new(struct section_file *file,
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

/************************************************************************//**
  Free resources allocated for savedata item
****************************************************************************/
static void savedata_destroy(struct savedata *saving)
{
  free(saving);
}

/* =======================================================================
 * Helper functions.
 * ======================================================================= */

/************************************************************************//**
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
  case 'a':
  case 'A':
    return ORDER_ACTIVITY;
  case 'x':
  case 'X':
    return ORDER_ACTION_MOVE;
  case 'p':
  case 'P':
    return ORDER_PERFORM_ACTION;
  }

  /* This can happen if the savegame is invalid. */
  return ORDER_LAST;
}

/************************************************************************//**
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
  case ORDER_ACTION_MOVE:
    return 'x';
  case ORDER_PERFORM_ACTION:
    return 'p';
  case ORDER_LAST:
    break;
  }

  fc_assert(FALSE);
  return '?';
}

/************************************************************************//**
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

/************************************************************************//**
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

/************************************************************************//**
  Returns a character identifier for an activity. See also char2activity().

  activity type is 'int', and not 'enum_activity' for supporting
  deprecated values (..._OLD_...)
****************************************************************************/
static char activity2char(int activity)
{
  switch (activity) {
  case ACTIVITY_IDLE:
    return 'w';
  case ACTIVITY_CLEAN:
    return 'C';
  case ACTIVITY_OLD_POLLUTION_SG3:
    return 'p';
  case ACTIVITY_MINE:
    return 'm';
  case ACTIVITY_PLANT:
    return 'M';
  case ACTIVITY_IRRIGATE:
    return 'i';
  case ACTIVITY_CULTIVATE:
    return 'I';
  case ACTIVITY_FORTIFIED:
    return 'f';
  case ACTIVITY_SENTRY:
    return 's';
  case ACTIVITY_PILLAGE:
    return 'e';
  case ACTIVITY_GOTO:
    return 'g';
  case ACTIVITY_EXPLORE:
    return 'x';
  case ACTIVITY_TRANSFORM:
    return 'o';
  case ACTIVITY_FORTIFYING:
    return 'y';
  case ACTIVITY_OLD_FALLOUT_SG3:
    return 'u';
  case ACTIVITY_BASE:
    return 'b';
  case ACTIVITY_GEN_ROAD:
    return 'R';
  case ACTIVITY_CONVERT:
    return 'c';
  case ACTIVITY_LAST:
    break;
  }

  fc_assert(FALSE);

  return '?';
}

/************************************************************************//**
  Returns an activity for a character identifier. See also activity2char().
****************************************************************************/
static enum unit_activity char2activity(char activity)
{
  enum unit_activity a;

  for (a = 0; a < ACTIVITY_LAST_SAVEGAME3; a++) {
    /* Skip ACTIVITY_LAST. The SAVEGAME3 specific values are after it. */
    if (a != ACTIVITY_LAST) {
      char achar = activity2char(a);

      if (activity == achar) {
        return a;
      }
    }
  }

  /* This can happen if the savegame is invalid. */
  return ACTIVITY_LAST;
}

/************************************************************************//**
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

/************************************************************************//**
  Unquote a string. The unquoted data is written into dest. If the unquoted
  data will be larger than dest_length the function aborts. It returns the
  actual length of the unquoted block.
****************************************************************************/
static int unquote_block(const char *const quoted_, void *dest,
                         int dest_length)
{
  int i, length, parsed, tmp;
  char *endptr;
  const char *quoted = quoted_;

  parsed = sscanf(quoted, "%d", &length);

  if (parsed != 1) {
    log_error(_("Syntax error in attribute block."));
    return 0;
  }

  if (length > dest_length) {
    return 0;
  }

  quoted = strchr(quoted, ':');

  if (quoted == NULL) {
    log_error(_("Syntax error in attribute block."));
    return 0;
  }

  quoted++;

  for (i = 0; i < length; i++) {
    tmp = strtol(quoted, &endptr, 16);

    if ((endptr - quoted) != 2
        || *endptr != ' '
        || (tmp & 0xff) != tmp) {
      log_error(_("Syntax error in attribute block."));
      return 0;
    }

    ((unsigned char *) dest)[i] = tmp;
    quoted += 3;
  }

  return length;
}

/************************************************************************//**
  Load the worklist elements specified by path to the worklist pointed to
  by 'pwl'. 'pwl' should be a pointer to an existing worklist.
****************************************************************************/
static void worklist_load(struct section_file *file, int wlist_max_length,
                          struct worklist *pwl, const char *path, ...)
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

  /* Padding entries */
  for (; i < wlist_max_length; i++) {
    secfile_entry_ignore(file, "%s.wl_kind%d", path_str, i);
    secfile_entry_ignore(file, "%s.wl_value%d", path_str, i);
  }
}

/************************************************************************//**
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

/************************************************************************//**
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

  whole_map_iterate(&(wld.map), ptile) {
    j = 0;
    unit_list_iterate(ptile->units, punit) {
      punit->server.ord_map = j++;
    } unit_list_iterate_end;
  } whole_map_iterate_end;
}

/************************************************************************//**
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

  whole_map_iterate(&(wld.map), ptile) {
    unit_list_sort_ord_map(ptile->units);
  } whole_map_iterate_end;
}

/************************************************************************//**
  Helper function for loading extras from a savegame.

  'ch' gives the character loaded from the savegame. Extras are packed
  in four to a character in hex notation. 'index' is a mapping of
  savegame bit -> base bit.
****************************************************************************/
static void sg_extras_set_dbv(struct dbv *extras, char ch,
                              struct extra_type **idx)
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
    struct extra_type *pextra = idx[i];

    if (pextra == NULL) {
      continue;
    }
    if ((bin & (1 << i))
        && (wld.map.server.have_huts || !is_extra_caused_by(pextra, EC_HUT))) {
      dbv_set(extras, extra_index(pextra));
    }
  }
}

/************************************************************************//**
  Helper function for loading extras from a savegame.

  'ch' gives the character loaded from the savegame. Extras are packed
  in four to a character in hex notation. 'index' is a mapping of
  savegame bit -> base bit.
****************************************************************************/
static void sg_extras_set_bv(bv_extras *extras, char ch,
                             struct extra_type **idx)
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
    struct extra_type *pextra = idx[i];

    if (pextra == NULL) {
      continue;
    }
    if ((bin & (1 << i))
        && (wld.map.server.have_huts || !is_extra_caused_by(pextra, EC_HUT))) {
      BV_SET(*extras, extra_index(pextra));
    }
  }
}

/************************************************************************//**
  Helper function for saving extras into a savegame.

  Extras are packed in four to a character in hex notation. 'index'
  specifies which set of extras are included in this character.
****************************************************************************/
static char sg_extras_get_dbv(struct dbv *extras, struct extra_type *presource,
                              const int *idx)
{
  int i, bin = 0;
  int max = dbv_bits(extras);

  for (i = 0; i < 4; i++) {
    int extra = idx[i];

    if (extra < 0) {
      break;
    }

    if (extra < max
        && (dbv_isset(extras, extra)
            /* An invalid resource, a resource that can't exist at the tile's
             * current terrain, isn't in the bit extra vector. Save it so it
             * can return if the tile's terrain changes to something it can
             * exist on. */
            || extra_by_number(extra) == presource)) {
      bin |= (1 << i);
    }
  }

  return hex_chars[bin];
}

/************************************************************************//**
  Helper function for saving extras into a savegame.

  Extras are packed in four to a character in hex notation. 'index'
  specifies which set of extras are included in this character.
****************************************************************************/
static char sg_extras_get_bv(bv_extras extras, struct extra_type *presource,
                             const int *idx)
{
  int i, bin = 0;

  for (i = 0; i < 4; i++) {
    int extra = idx[i];

    if (extra < 0) {
      break;
    }

    if (BV_ISSET(extras, extra)
        /* An invalid resource, a resource that can't exist at the tile's
         * current terrain, isn't in the bit extra vector. Save it so it
         * can return if the tile's terrain changes to something it can
         * exist on. */
        || extra_by_number(extra) == presource) {
      bin |= (1 << i);
    }
  }

  return hex_chars[bin];
}

/************************************************************************//**
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
    if (pterrain->identifier_load == ch) {
      return pterrain;
    }
  } terrain_type_iterate_end;

  log_fatal("Unknown terrain identifier '%c' in savegame.", ch);

  exit(EXIT_FAILURE);

  RETURN_VALUE_AFTER_EXIT(NULL);
}

/************************************************************************//**
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

/************************************************************************//**
  Load technology from path + "_name".
****************************************************************************/
static Tech_type_id technology_load(struct section_file *file,
                                    const char *path, int plrno)
{
  char path_with_name[128];
  const char *name;
  struct advance *padvance;

  fc_snprintf(path_with_name, sizeof(path_with_name),
              "%s_name", path);

  name = secfile_lookup_str(file, path_with_name, plrno);

  if (!name || name[0] == '\0') {
    /* Used by researching_saved */
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

/************************************************************************//**
  Save technology in secfile entry called path_name.
****************************************************************************/
static void technology_save(struct section_file *file,
                            const char *path, int plrno, Tech_type_id tech)
{
  char path_with_name[128];
  const char *name;

  fc_snprintf(path_with_name, sizeof(path_with_name),
              "%s_name", path);

  switch (tech) {
    case A_UNKNOWN: /* Used by researching_saved */
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

/************************************************************************//**
  Load '[savefile]'.
****************************************************************************/
static void sg_load_savefile(struct loaddata *loading)
{
  int i;
  const char *terr_name;
  bool ruleset_datafile;
  bool current_ruleset_rejected;
  const char *str;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  /* Load savefile options. */
  loading->secfile_options
    = secfile_lookup_str(loading->file, "savefile.options");

  /* We don't need these entries, but read them anyway to avoid
   * warnings about unread secfile entries. */
  secfile_entry_ignore_by_path(loading->file, "savefile.reason");
  secfile_entry_ignore_by_path(loading->file, "savefile.revision");

  str = secfile_lookup_str(loading->file, "savefile.orig_version");
  sz_strlcpy(game.server.orig_game_version, str);

  if (game.scenario.datafile[0] != '\0') {
    ruleset_datafile = FALSE;
  } else {
    ruleset_datafile = TRUE;
  }

  current_ruleset_rejected = FALSE;
  if (game.scenario.is_scenario && !game.scenario.ruleset_locked) {
    const char *req_caps;

    if (!load_rulesets(NULL, NULL, FALSE, NULL, TRUE, FALSE,
                       ruleset_datafile)) {
      /* Failed to load correct ruleset */
      sg_failure_ret(FALSE, _("Failed to load ruleset '%s'."),
                     game.server.rulesetdir);
    }

    req_caps = secfile_lookup_str_default(loading->file, "",
                                          "scenario.ruleset_caps");
    strncpy(game.scenario.req_caps, req_caps,
            sizeof(game.scenario.req_caps) - 1);
    game.scenario.req_caps[sizeof(game.scenario.req_caps) - 1] = '\0';

    if (!has_capabilities(req_caps, game.ruleset_capabilities)) {
      /* Current ruleset lacks required capabilities. */
      log_normal(_("Scenario requires ruleset capabilities: %s"), req_caps);
      log_normal(_("Ruleset has capabilities: %s"),
                 game.ruleset_capabilities);
      /* TRANS: ... ruleset dir ... scenario name ... */
      log_error(_("Current ruleset %s not compatible with the scenario %s."
                  " Trying to switch to the ruleset specified by the"
                  " scenario."),
                game.server.rulesetdir, game.scenario.name);

      current_ruleset_rejected = TRUE;
    }
  }

  if (!game.scenario.is_scenario || game.scenario.ruleset_locked
      || current_ruleset_rejected) {
    const char *ruleset, *alt_dir;

    ruleset = secfile_lookup_str_default(loading->file,
                                         GAME_DEFAULT_RULESETDIR,
                                         "savefile.rulesetdir");

    /* Load ruleset. */
    sz_strlcpy(game.server.rulesetdir, ruleset);
    if (!strcmp("default", game.server.rulesetdir)) {
      /* Here 'default' really means current default.
       * Saving happens with real ruleset name, so savegames containing this
       * are special scenarios. */
      sz_strlcpy(game.server.rulesetdir, GAME_DEFAULT_RULESETDIR);
      log_verbose("Savegame specified ruleset '%s'. Really loading '%s'.",
                  ruleset, game.server.rulesetdir);
    }

    alt_dir = secfile_lookup_str_default(loading->file, NULL,
                                         "savefile.ruleset_alt_dir");

    if (!load_rulesets(NULL, alt_dir, FALSE, NULL, TRUE, FALSE, ruleset_datafile)) {
      if (alt_dir) {
        sg_failure_ret(FALSE,
                       _("Failed to load either of rulesets '%s' or '%s' "
                         "needed for savegame."),
                       ruleset, alt_dir);
      } else {
        sg_failure_ret(FALSE,
                       _("Failed to load ruleset '%s' needed for savegame."),
                       ruleset);
      }
    }

    if (current_ruleset_rejected) {
      /* TRANS: ruleset dir */
      log_normal(_("Successfully loaded the scenario's ruleset %s."), ruleset);
    }
  }

  /* Remove all aifill players. Correct number of them get created later
   * with correct skill level etc. */
  (void) aifill(0);

  /* Time to load scenario specific luadata */
  if (game.scenario.datafile[0] != '\0') {
    if (!fc_strcasecmp("none", game.scenario.datafile)) {
      game.server.luadata = NULL;
    } else {
      const struct strvec *paths[] = { get_scenario_dirs(), NULL };
      const struct strvec **path;
      const char *found = NULL;
      char testfile[MAX_LEN_PATH];
      struct section_file *secfile;

      for (path = paths; found == NULL && *path != NULL; path++) {
        fc_snprintf(testfile, sizeof(testfile), "%s.luadata", game.scenario.datafile);

        found = fileinfoname(*path, testfile);
      }

      if (found == NULL) {
        log_error(_("Can't find scenario luadata file %s.luadata."), game.scenario.datafile);
        sg_success = FALSE;
        return;
      }

      secfile = secfile_load(found, FALSE);
      if (secfile == NULL) {
        log_error(_("Failed to load scenario luadata file %s.luadata"),
                  game.scenario.datafile);
        sg_success = FALSE;
        return;
      }

      game.server.luadata = secfile;
    }
  }

  game.server.dbid = secfile_lookup_int_default(loading->file, -1,
                                                "savefile.dbid");

  /* This is in the savegame only if the game has been started before savegame3.c time,
   * and in that case it's TRUE. If it's missing, it's to be considered FALSE. */
  game.server.last_updated_year = secfile_lookup_bool_default(loading->file, FALSE,
                                                       "savefile.last_updated_as_year");

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
    sg_failure_ret(loading->technology.size != 0,
                   "Failed to load technology order: %s",
                   secfile_error());
  }

  /* Load Activities. */
  loading->activities.size
    = secfile_lookup_int_default(loading->file, 0,
                                 "savefile.activities_size");
  if (loading->activities.size) {
    loading->activities.order
      = secfile_lookup_str_vec(loading->file, &loading->activities.size,
                               "savefile.activities_vector");
    sg_failure_ret(loading->activities.size != 0,
                   "Failed to load activity order: %s",
                   secfile_error());
  }

  /* Load traits. */
  loading->trait.size
    = secfile_lookup_int_default(loading->file, 0,
                                 "savefile.trait_size");
  if (loading->trait.size) {
    loading->trait.order
      = secfile_lookup_str_vec(loading->file, &loading->trait.size,
                               "savefile.trait_vector");
    sg_failure_ret(loading->trait.size != 0,
                   "Failed to load trait order: %s",
                   secfile_error());
  }

  /* Load extras. */
  loading->extra.size
    = secfile_lookup_int_default(loading->file, 0,
                                 "savefile.extras_size");
  if (loading->extra.size) {
    const char **modname;
    size_t nmod;
    int j;

    modname = secfile_lookup_str_vec(loading->file, &loading->extra.size,
                                     "savefile.extras_vector");
    sg_failure_ret(loading->extra.size != 0,
                   "Failed to load extras order: %s",
                   secfile_error());
    sg_failure_ret(!(game.control.num_extra_types < loading->extra.size),
                   "Number of extras defined by the ruleset (= %d) are "
                   "lower than the number in the savefile (= %d).",
                   game.control.num_extra_types, (int)loading->extra.size);
    /* make sure that the size of the array is divisible by 4 */
    nmod = 4 * ((loading->extra.size + 3) / 4);
    loading->extra.order = fc_calloc(nmod, sizeof(*loading->extra.order));
    for (j = 0; j < loading->extra.size; j++) {
      loading->extra.order[j] = extra_type_by_rule_name(modname[j]);
    }
    free(modname);
    for (; j < nmod; j++) {
      loading->extra.order[j] = NULL;
    }
  }

  /* Load multipliers. */
  loading->multiplier.size
    = secfile_lookup_int_default(loading->file, 0,
                                 "savefile.multipliers_size");
  if (loading->multiplier.size) {
    const char **modname;
    int j;

    modname = secfile_lookup_str_vec(loading->file, &loading->multiplier.size,
                                     "savefile.multipliers_vector");
    sg_failure_ret(loading->multiplier.size != 0,
                   "Failed to load multipliers order: %s",
                   secfile_error());
    /* It's OK for the set of multipliers in the savefile to differ
     * from those in the ruleset. */
    loading->multiplier.order = fc_calloc(loading->multiplier.size,
                                          sizeof(*loading->multiplier.order));
    for (j = 0; j < loading->multiplier.size; j++) {
      loading->multiplier.order[j] = multiplier_by_rule_name(modname[j]);
      if (!loading->multiplier.order[j]) {
        log_verbose("Multiplier \"%s\" in savegame but not in ruleset, "
                    "discarding", modname[j]);
      }
    }
    free(modname);
  }

  /* Load specialists. */
  loading->specialist.size
    = secfile_lookup_int_default(loading->file, 0,
                                 "savefile.specialists_size");
  if (loading->specialist.size) {
    const char **modname;
    size_t nmod;
    int j;

    modname = secfile_lookup_str_vec(loading->file, &loading->specialist.size,
                                     "savefile.specialists_vector");
    sg_failure_ret(loading->specialist.size != 0,
                   "Failed to load specialists order: %s",
                   secfile_error());
    sg_failure_ret(!(game.control.num_specialist_types < loading->specialist.size),
                   "Number of specialists defined by the ruleset (= %d) are "
                   "lower than the number in the savefile (= %d).",
                   game.control.num_specialist_types, (int)loading->specialist.size);
    /* make sure that the size of the array is divisible by 4 */
    /* That's not really needed with specialists at the moment, but done this way
     * for consistency with other types, and to be prepared for the time it needs
     * to be this way. */
    nmod = 4 * ((loading->specialist.size + 3) / 4);
    loading->specialist.order = fc_calloc(nmod, sizeof(*loading->specialist.order));
    for (j = 0; j < loading->specialist.size; j++) {
      loading->specialist.order[j] = specialist_by_rule_name(modname[j]);
    }
    free(modname);
    for (; j < nmod; j++) {
      loading->specialist.order[j] = NULL;
    }
  }

  /* Load action order. */
  loading->action.size = secfile_lookup_int_default(loading->file, 0,
                                                    "savefile.action_size");

  sg_failure_ret(loading->action.size > 0,
                 "Failed to load action order: %s",
                 secfile_error());

  if (loading->action.size) {
    const char **modname;
    int j;

    modname = secfile_lookup_str_vec(loading->file, &loading->action.size,
                                     "savefile.action_vector");

    loading->action.order = fc_calloc(loading->action.size,
                                      sizeof(*loading->action.order));

    for (j = 0; j < loading->action.size; j++) {
      struct action *real_action = action_by_rule_name(modname[j]);

      if (real_action) {
        loading->action.order[j] = real_action->id;
      } else {
        log_sg("Unknown action \'%s\'", modname[j]);
        loading->action.order[j] = ACTION_NONE;
      }
    }

    free(modname);
  }

  /* Load action decision order. */
  loading->act_dec.size
    = secfile_lookup_int_default(loading->file, 0,
                                 "savefile.action_decision_size");

  sg_failure_ret(loading->act_dec.size > 0,
                 "Failed to load action decision order: %s",
                 secfile_error());

  if (loading->act_dec.size) {
    const char **modname;
    int j;

    modname = secfile_lookup_str_vec(loading->file, &loading->act_dec.size,
                                     "savefile.action_decision_vector");

    loading->act_dec.order = fc_calloc(loading->act_dec.size,
                                       sizeof(*loading->act_dec.order));

    for (j = 0; j < loading->act_dec.size; j++) {
      loading->act_dec.order[j] = action_decision_by_name(modname[j],
                                                          fc_strcasecmp);
    }

    free(modname);
  }

  /* Load server side agent order. */
  loading->ssa.size
      = secfile_lookup_int_default(loading->file, 0,
                                   "savefile.server_side_agent_size");

  sg_failure_ret(loading->ssa.size > 0,
                 "Failed to load server side agent order: %s",
                 secfile_error());

  if (loading->ssa.size) {
    const char **modname;
    int j;

    modname = secfile_lookup_str_vec(loading->file, &loading->ssa.size,
                                     "savefile.server_side_agent_list");

    loading->ssa.order = fc_calloc(loading->ssa.size,
                                   sizeof(*loading->ssa.order));

    for (j = 0; j < loading->ssa.size; j++) {
      loading->ssa.order[j] = server_side_agent_by_name(modname[j],
                                                        fc_strcasecmp);
    }

    free(modname);
  }

  /* Load city options order. */
  loading->coptions.size
    = secfile_lookup_int_default(loading->file, 0,
                                 "savefile.city_options_size");

  sg_failure_ret(loading->coptions.size > 0,
                 "Failed to load city options order: %s",
                 secfile_error());

  if (loading->coptions.size) {
    const char **modname;
    int j;

    modname = secfile_lookup_str_vec(loading->file, &loading->coptions.size,
                                     "savefile.city_options_vector");

    loading->coptions.order = fc_calloc(loading->coptions.size,
                                        sizeof(*loading->coptions.order));

    for (j = 0; j < loading->coptions.size; j++) {
      loading->coptions.order[j] = city_options_by_name(modname[j],
                                                        fc_strcasecmp);
    }

    free(modname);
  }

  /* Terrain identifiers */
  terrain_type_iterate(pterr) {
    pterr->identifier_load = '\0';
  } terrain_type_iterate_end;

  i = 0;
  while ((terr_name = secfile_lookup_str_default(loading->file, NULL,
                                                 "savefile.terrident%d.name", i)) != NULL) {
    struct terrain *pterr = terrain_by_rule_name(terr_name);

    if (pterr != NULL) {
      const char *iptr =  secfile_lookup_str_default(loading->file, NULL,
                                                     "savefile.terrident%d.identifier", i);

      pterr->identifier_load = *iptr;
    } else {
      log_error("Identifier for unknown terrain type %s.", terr_name);
    }
    i++;
  }

  terrain_type_iterate(pterr) {
    terrain_type_iterate(pterr2) {
      if (pterr != pterr2  && pterr->identifier_load != '\0') {
        sg_failure_ret((pterr->identifier_load != pterr2->identifier_load),
                       "%s and %s share a saved identifier",
                       terrain_rule_name(pterr), terrain_rule_name(pterr2));
      }
    } terrain_type_iterate_end;
  } terrain_type_iterate_end;
}

/************************************************************************//**
  Save '[savefile]'.
****************************************************************************/
static void sg_save_savefile(struct savedata *saving)
{
  int i;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  /* Save savefile options. */
  sg_save_savefile_options(saving, savefile_options_default);

  secfile_insert_int(saving->file, current_compat_ver(), "savefile.version");

  /* Save reason of the savefile generation. */
  secfile_insert_str(saving->file, saving->save_reason, "savefile.reason");

  /* Save as accurate freeciv revision information as possible */
  secfile_insert_str(saving->file, freeciv_datafile_version(), "savefile.revision");

  /* Freeciv version used in the very first launch of this game -
   * or even saving in pregame. */
  secfile_insert_str(saving->file, game.server.orig_game_version,
                     "savefile.orig_version");

  /* Save rulesetdir at this point as this ruleset is required by this
   * savefile. */
  secfile_insert_str(saving->file, game.server.rulesetdir, "savefile.rulesetdir");

  if (game.control.version[0] != '\0') {
    /* Current ruleset has version information, save it.
     * This is never loaded, but exist in savegame file only for debugging purposes. */
    secfile_insert_str(saving->file, game.control.version, "savefile.rulesetversion");
  }

  if (game.control.alt_dir[0] != '\0') {
    secfile_insert_str(saving->file, game.control.alt_dir, "savefile.ruleset_alt_dir");
  }

  if (game.server.last_updated_year) {
    secfile_insert_bool(saving->file, TRUE, "savefile.last_updated_as_year");
  }

  secfile_insert_int(saving->file, game.server.dbid, "savefile.dbid");

  /* Save improvement order in savegame, so we are not dependent on ruleset
   * order. If the game isn't started improvements aren't loaded so we can
   * not save the order. */
  secfile_insert_int(saving->file, improvement_count(),
                     "savefile.improvement_size");
  if (improvement_count() > 0) {
    const char *buf[improvement_count()];

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
    const char *buf[game.control.num_tech_types];

    buf[A_NONE] = "A_NONE";
    advance_iterate(a) {
      buf[advance_index(a)] = advance_rule_name(a);
    } advance_iterate_end;
    secfile_insert_str_vec(saving->file, buf, game.control.num_tech_types,
                           "savefile.technology_vector");
  }

  /* Save activities order in the savegame. */
  secfile_insert_int(saving->file, ACTIVITY_LAST,
                     "savefile.activities_size");
  if (ACTIVITY_LAST > 0) {
    const char **modname;
    int j;

    i = 0;

    modname = fc_calloc(ACTIVITY_LAST, sizeof(*modname));

    for (j = 0; j < ACTIVITY_LAST; j++) {
      modname[i++] = unit_activity_name(j);
    }

    secfile_insert_str_vec(saving->file, modname,
                           ACTIVITY_LAST,
                           "savefile.activities_vector");
    free(modname);
  }

  /* Save specialists order in the savegame. */
  secfile_insert_int(saving->file, specialist_count(),
                     "savefile.specialists_size");
  {
    const char **modname;

    i = 0;
    modname = fc_calloc(specialist_count(), sizeof(*modname));

    specialist_type_iterate(sp) {
      modname[i++] = specialist_rule_name(specialist_by_number(sp));
    } specialist_type_iterate_end;

    secfile_insert_str_vec(saving->file, modname, specialist_count(),
                           "savefile.specialists_vector");

    free(modname);
  }

  /* Save trait order in savegame. */
  secfile_insert_int(saving->file, TRAIT_COUNT,
                     "savefile.trait_size");
  {
    const char **modname;
    enum trait tr;
    int j;

    modname = fc_calloc(TRAIT_COUNT, sizeof(*modname));

    for (tr = trait_begin(), j = 0; tr != trait_end(); tr = trait_next(tr), j++) {
      modname[j] = trait_name(tr);
    }

    secfile_insert_str_vec(saving->file, modname, TRAIT_COUNT,
                           "savefile.trait_vector");
    free(modname);
  }

  /* Save extras order in the savegame. */
  secfile_insert_int(saving->file, game.control.num_extra_types,
                     "savefile.extras_size");
  if (game.control.num_extra_types > 0) {
    const char **modname;

    i = 0;
    modname = fc_calloc(game.control.num_extra_types, sizeof(*modname));

    extra_type_iterate(pextra) {
      modname[i++] = extra_rule_name(pextra);
    } extra_type_iterate_end;

    secfile_insert_str_vec(saving->file, modname,
                           game.control.num_extra_types,
                           "savefile.extras_vector");
    free(modname);
  }

  /* Save multipliers order in the savegame. */
  secfile_insert_int(saving->file, multiplier_count(),
                     "savefile.multipliers_size");
  if (multiplier_count() > 0) {
    const char **modname;

    modname = fc_calloc(multiplier_count(), sizeof(*modname));

    multipliers_iterate(pmul) {
      modname[multiplier_index(pmul)] = multiplier_rule_name(pmul);
    } multipliers_iterate_end;

    secfile_insert_str_vec(saving->file, modname,
                           multiplier_count(),
                           "savefile.multipliers_vector");
    free(modname);
  }

  /* Save city_option order in the savegame. */
  secfile_insert_int(saving->file, CITYO_LAST,
                     "savefile.city_options_size");
  if (CITYO_LAST > 0) {
    const char **modname;
    int j;

    i = 0;
    modname = fc_calloc(CITYO_LAST, sizeof(*modname));

    for (j = 0; j < CITYO_LAST; j++) {
      modname[i++] = city_options_name(j);
    }

    secfile_insert_str_vec(saving->file, modname,
                           CITYO_LAST,
                           "savefile.city_options_vector");
    free(modname);
  }

  /* Save action order in the savegame. */
  secfile_insert_int(saving->file, NUM_ACTIONS,
                     "savefile.action_size");
  if (NUM_ACTIONS > 0) {
    const char **modname;
    int j;

    i = 0;
    modname = fc_calloc(NUM_ACTIONS, sizeof(*modname));

    for (j = 0; j < NUM_ACTIONS; j++) {
      modname[i++] = action_id_rule_name(j);
    }

    secfile_insert_str_vec(saving->file, modname,
                           NUM_ACTIONS,
                           "savefile.action_vector");
    free(modname);
  }

  /* Save action decision order in the savegame. */
  secfile_insert_int(saving->file, ACT_DEC_COUNT,
                     "savefile.action_decision_size");
  if (ACT_DEC_COUNT > 0) {
    const char **modname;
    int j;

    i = 0;
    modname = fc_calloc(ACT_DEC_COUNT, sizeof(*modname));

    for (j = 0; j < ACT_DEC_COUNT; j++) {
      modname[i++] = action_decision_name(j);
    }

    secfile_insert_str_vec(saving->file, modname,
                           ACT_DEC_COUNT,
                           "savefile.action_decision_vector");
    free(modname);
  }

  /* Save server side agent order in the savegame. */
  secfile_insert_int(saving->file, SSA_COUNT,
                     "savefile.server_side_agent_size");
  if (SSA_COUNT > 0) {
    const char **modname;
    int j;

    i = 0;
    modname = fc_calloc(SSA_COUNT, sizeof(*modname));

    for (j = 0; j < SSA_COUNT; j++) {
      modname[i++] = server_side_agent_name(j);
    }

    secfile_insert_str_vec(saving->file, modname,
                           SSA_COUNT,
                           "savefile.server_side_agent_list");
    free(modname);
  }

  /* Save terrain character mapping in the savegame. */
  i = 0;
  terrain_type_iterate(pterr) {
    char buf[2];

    secfile_insert_str(saving->file, terrain_rule_name(pterr), "savefile.terrident%d.name", i);
    buf[0] = terrain_identifier(pterr);
    buf[1] = '\0';
    secfile_insert_str(saving->file, buf, "savefile.terrident%d.identifier", i++);
  } terrain_type_iterate_end;
}

/************************************************************************//**
  Save options for this savegame. sg_load_savefile_options() is not defined.
****************************************************************************/
static void sg_save_savefile_options(struct savedata *saving,
                                     const char *option)
{
  /* Check status and return if not OK (sg_success FALSE). */
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

/************************************************************************//**
  Load '[ruledata]'.
****************************************************************************/
static void sg_load_ruledata(struct loaddata *loading)
{
  int i;
  const char *name;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  for (i = 0;
       (name = secfile_lookup_str_default(loading->file, NULL,
                                          "ruledata.government%d.name", i));
       i++) {
    struct government *gov = government_by_rule_name(name);

    if (gov != NULL) {
      gov->changed_to_times = secfile_lookup_int_default(loading->file, 0,
                                                         "ruledata.government%d.changes", i);
    }
  }
}

/************************************************************************//**
  Load '[game]'.
****************************************************************************/
static void sg_load_game(struct loaddata *loading)
{
  const char *str;
  int i;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  /* Load server state. */
  str = secfile_lookup_str_default(loading->file, "S_S_INITIAL",
                                   "game.server_state");
  loading->server_state = server_states_by_name(str, strcmp);
  if (!server_states_is_valid(loading->server_state)) {
    /* Don't take any risk! */
    loading->server_state = S_S_INITIAL;
  }

  game.server.additional_phase_seconds
    = secfile_lookup_int_default(loading->file, 0, "game.phase_seconds");

  str = secfile_lookup_str_default(loading->file,
                                   default_meta_patches_string(),
                                   "game.meta_patches");
  set_meta_patches_string(str);

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

  str = secfile_lookup_str_default(loading->file, NULL,
                                   "game.phase_mode");
  if (str != NULL) {
    game.info.phase_mode = phase_mode_type_by_name(str, fc_strcasecmp);
    if (!phase_mode_type_is_valid(game.info.phase_mode)) {
      log_error("Illegal phase mode \"%s\"", str);
      game.info.phase_mode = GAME_DEFAULT_PHASE_MODE;
    }
  } else {
    log_error("Phase mode missing");
  }

  str  = secfile_lookup_str_default(loading->file, NULL,
                                    "game.phase_mode_stored");
  if (str != NULL) {
    game.server.phase_mode_stored = phase_mode_type_by_name(str, fc_strcasecmp);
    if (!phase_mode_type_is_valid(game.server.phase_mode_stored)) {
      log_error("Illegal stored phase mode \"%s\"", str);
      game.server.phase_mode_stored = GAME_DEFAULT_PHASE_MODE;
    }
  } else {
    log_error("Stored phase mode missing");
  }
  game.info.phase
    = secfile_lookup_int_default(loading->file, 0,
                                 "game.phase");
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
  sg_failure_ret(secfile_lookup_int(loading->file, &game.server.world_peace_start,
                                    "game.world_peace_start"), "%s", secfile_error());
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

  /* Savegame may have stored random_seed for documentation purposes only,
   * but we want to keep it for resaving. */
  game.server.seed = secfile_lookup_int_default(loading->file, 0, "game.random_seed");

  /* Global advances. */
  str = secfile_lookup_str_default(loading->file, NULL,
                                   "game.global_advances");
  if (str != NULL) {
    sg_failure_ret(strlen(str) == loading->technology.size,
                   "Invalid length of 'game.global_advances' ("
                   SIZE_T_PRINTF " ~= " SIZE_T_PRINTF ").",
                   strlen(str), loading->technology.size);
    for (i = 0; i < loading->technology.size; i++) {
      sg_failure_ret(str[i] == '1' || str[i] == '0',
                     "Undefined value '%c' within 'game.global_advances'.",
                     str[i]);
      if (str[i] == '1') {
        struct advance *padvance
          = advance_by_rule_name(loading->technology.order[i]);

        if (padvance != NULL) {
          game.info.global_advances[advance_number(padvance)] = TRUE;
        }
      }
    }
  }

  game.info.is_new_game
    = !secfile_lookup_bool_default(loading->file, TRUE, "game.save_players");

  game.server.turn_change_time
    = secfile_lookup_float_default(loading->file, 0, "game.last_turn_change_time");
}

/************************************************************************//**
  Save '[ruledata]'.
****************************************************************************/
static void sg_save_ruledata(struct savedata *saving)
{
  int set_count = 0;

  governments_iterate(pgov) {
     char path[256];

     fc_snprintf(path, sizeof(path),
                 "ruledata.government%d", set_count++);

     secfile_insert_str(saving->file, government_rule_name(pgov),
                        "%s.name", path);
     secfile_insert_int(saving->file, pgov->changed_to_times,
                        "%s.changes", path);
  } governments_iterate_end;
}

/************************************************************************//**
  Save '[game]'.
****************************************************************************/
static void sg_save_game(struct savedata *saving)
{
  enum server_states srv_state;
  char global_advances[game.control.num_tech_types + 1];
  int i;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

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

  if (game.server.phase_timer != NULL) {
    secfile_insert_int(saving->file,
                       timer_read_seconds(game.server.phase_timer)
                       + game.server.additional_phase_seconds,
                       "game.phase_seconds");
  }

  secfile_insert_str(saving->file, get_meta_patches_string(),
                     "game.meta_patches");
  secfile_insert_str(saving->file, meta_addr_port(), "game.meta_server");

  secfile_insert_str(saving->file, server.game_identifier, "game.id");
  secfile_insert_str(saving->file, srvarg.serverid, "game.serverid");

  secfile_insert_str(saving->file,
                     phase_mode_type_name(game.info.phase_mode),
                     "game.phase_mode");
  secfile_insert_str(saving->file,
                     phase_mode_type_name(game.server.phase_mode_stored),
                     "game.phase_mode_stored");
  secfile_insert_int(saving->file, game.info.phase,
                     "game.phase");
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
  secfile_insert_int(saving->file, game.server.world_peace_start, "game.world_peace_start");
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
  /* For debugging purposes only.
   * Do not save it if it's 0 (not known);
   * this confuses people reading this 'document' less than
   * saving 0. */
  if (game.server.seed != 0) {
    secfile_insert_int(saving->file, game.server.seed,
                       "game.random_seed");
  }

  /* Global advances. */
  for (i = 0; i < game.control.num_tech_types; i++) {
    global_advances[i] = game.info.global_advances[i] ? '1' : '0';
  }
  global_advances[i] = '\0';
  secfile_insert_str(saving->file, global_advances, "game.global_advances");

  if (!game_was_started()) {
    saving->save_players = FALSE;
  } else {
    if (saving->scenario) {
      saving->save_players = game.scenario.players;
    } else {
      saving->save_players = TRUE;
    }
#ifndef SAVE_DUMMY_TURN_CHANGE_TIME
    secfile_insert_float(saving->file, game.server.turn_change_time,
                         "game.last_turn_change_time");
#else  /* SAVE_DUMMY_TURN_CHANGE_TIME */
    secfile_insert_float(saving->file, game.info.turn * 0.1,
                         "game.last_turn_change_time");
#endif /* SAVE_DUMMY_TURN_CHANGE_TIME */
  }
  secfile_insert_bool(saving->file, saving->save_players,
                      "game.save_players");

  if (srv_state != S_S_INITIAL) {
    const char *ainames[ai_type_get_count()];

    i = 0;
    ai_type_iterate(ait) {
      ainames[i] = ait->name;
      i++;
    } ai_type_iterate_end;

    secfile_insert_str_vec(saving->file, ainames, i,
                           "game.ai_types");
  }
}

/* =======================================================================
 * Load / save random status.
 * ======================================================================= */

/************************************************************************//**
  Load '[random]'.
****************************************************************************/
static void sg_load_random(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  if (secfile_lookup_bool_default(loading->file, FALSE, "random.saved")) {
    const char *str;
    int i;

    sg_failure_ret(secfile_lookup_int(loading->file, &loading->rstate.j,
                                      "random.index_J"), "%s", secfile_error());
    sg_failure_ret(secfile_lookup_int(loading->file, &loading->rstate.k,
                                      "random.index_K"), "%s", secfile_error());
    sg_failure_ret(secfile_lookup_int(loading->file, &loading->rstate.x,
                                      "random.index_X"), "%s", secfile_error());

    for (i = 0; i < 8; i++) {
      str = secfile_lookup_str(loading->file, "random.table%d",i);
      sg_failure_ret(NULL != str, "%s", secfile_error());
      sscanf(str, "%8x %8x %8x %8x %8x %8x %8x", &loading->rstate.v[7*i],
             &loading->rstate.v[7*i+1], &loading->rstate.v[7*i+2],
             &loading->rstate.v[7*i+3], &loading->rstate.v[7*i+4],
             &loading->rstate.v[7*i+5], &loading->rstate.v[7*i+6]);
    }
    loading->rstate.is_init = TRUE;
    fc_rand_set_state(loading->rstate);
  } else {
    /* No random values - mark the setting. */
    secfile_entry_ignore_by_path(loading->file, "random.saved");

    /* We're loading a game without a seed (which is okay, if it's a scenario).
     * We need to generate the game seed now because it will be needed later
     * during the load. */
    init_game_seed();
    loading->rstate = fc_rand_state();
  }
}

/************************************************************************//**
  Save '[random]'.
****************************************************************************/
static void sg_save_random(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  if (fc_rand_is_init() && (!saving->scenario || game.scenario.save_random)) {
    int i;
    RANDOM_STATE rstate = fc_rand_state();

    secfile_insert_bool(saving->file, TRUE, "random.saved");
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
    secfile_insert_bool(saving->file, FALSE, "random.saved");
  }
}

/* =======================================================================
 * Load / save lua script data.
 * ======================================================================= */

/************************************************************************//**
  Load '[script]'.
****************************************************************************/
static void sg_load_script(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  script_server_state_load(loading->file);
}

/************************************************************************//**
  Save '[script]'.
****************************************************************************/
static void sg_save_script(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  script_server_state_save(saving->file);
}

/* =======================================================================
 * Load / save scenario data.
 * ======================================================================= */

/************************************************************************//**
  Load '[scenario]'.
****************************************************************************/
static void sg_load_scenario(struct loaddata *loading)
{
  const char *buf;
  int game_version;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  /* Load version. */
  game_version
    = secfile_lookup_int_default(loading->file, 0, "scenario.game_version");
  /* We require at least version 2.90.99 - and at that time we saved version
   * numbers as 10000*MAJOR+100*MINOR+PATCH */
  sg_failure_ret(29099 <= game_version, "Saved game is too old, at least "
                                        "version 2.90.99 required.");

  loading->full_version = game_version;

  game.scenario.datafile[0] = '\0';

  sg_failure_ret(secfile_lookup_bool(loading->file, &game.scenario.is_scenario,
                                     "scenario.is_scenario"), "%s", secfile_error());
  if (!game.scenario.is_scenario) {
    return;
  }

  buf = secfile_lookup_str_default(loading->file, "", "scenario.name");
  if (buf[0] != '\0') {
    sz_strlcpy(game.scenario.name, buf);
  }

  buf = secfile_lookup_str_default(loading->file, "",
                                   "scenario.authors");
  if (buf[0] != '\0') {
    sz_strlcpy(game.scenario.authors, buf);
  } else {
    game.scenario.authors[0] = '\0';
  }

  buf = secfile_lookup_str_default(loading->file, "",
                                   "scenario.description");
  if (buf[0] != '\0') {
    sz_strlcpy(game.scenario_desc.description, buf);
  } else {
    game.scenario_desc.description[0] = '\0';
  }
  game.scenario.save_random
    = secfile_lookup_bool_default(loading->file, FALSE, "scenario.save_random");
  game.scenario.players
    = secfile_lookup_bool_default(loading->file, TRUE, "scenario.players");
  game.scenario.startpos_nations
    = secfile_lookup_bool_default(loading->file, FALSE,
                                  "scenario.startpos_nations");
  game.scenario.prevent_new_cities
    = secfile_lookup_bool_default(loading->file, FALSE,
                                  "scenario.prevent_new_cities");
  game.scenario.lake_flooding
    = secfile_lookup_bool_default(loading->file, TRUE,
                                  "scenario.lake_flooding");
  game.scenario.handmade
    = secfile_lookup_bool_default(loading->file, FALSE,
                                  "scenario.handmade");
  game.scenario.allow_ai_type_fallback
    = secfile_lookup_bool_default(loading->file, FALSE,
                                  "scenario.allow_ai_type_fallback");

  game.scenario.ruleset_locked
    = secfile_lookup_bool_default(loading->file, TRUE,
                                  "scenario.ruleset_locked");

  buf = secfile_lookup_str_default(loading->file, "",
                                   "scenario.datafile");
  if (buf[0] != '\0') {
    sz_strlcpy(game.scenario.datafile, buf);
  }

  sg_failure_ret(loading->server_state == S_S_INITIAL
                 || (loading->server_state == S_S_RUNNING
                     && game.scenario.players),
                 "Invalid scenario definition (server state '%s' and "
                 "players are %s).",
                 server_states_name(loading->server_state),
                 game.scenario.players ? "saved" : "not saved");
}

/************************************************************************//**
  Save '[scenario]'.
****************************************************************************/
static void sg_save_scenario(struct savedata *saving)
{
  struct entry *mod_entry;
  int game_version;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  game_version = MAJOR_VERSION * 1000000 + MINOR_VERSION * 10000 + PATCH_VERSION * 100;
#ifdef EMERGENCY_VERSION
  game_version += EMERGENCY_VERSION;
#endif /* EMERGENCY_VERSION */
  secfile_insert_int(saving->file, game_version, "scenario.game_version");

  if (!saving->scenario || !game.scenario.is_scenario) {
    secfile_insert_bool(saving->file, FALSE, "scenario.is_scenario");
    return;
  }

  secfile_insert_bool(saving->file, TRUE, "scenario.is_scenario");

  /* Name is mandatory to the level that is saved even if empty. */
  mod_entry = secfile_insert_str(saving->file, game.scenario.name, "scenario.name");
  entry_str_set_gt_marking(mod_entry, TRUE);

  /* Authors list is saved only if it exist */
  if (game.scenario.authors[0] != '\0') {
    mod_entry = secfile_insert_str(saving->file, game.scenario.authors,
                                   "scenario.authors");
    entry_str_set_gt_marking(mod_entry, TRUE);
  }

  /* Description is saved only if it exist */
  if (game.scenario_desc.description[0] != '\0') {
    mod_entry = secfile_insert_str(saving->file, game.scenario_desc.description,
                                   "scenario.description");
    entry_str_set_gt_marking(mod_entry, TRUE);
  }

  secfile_insert_bool(saving->file, game.scenario.save_random, "scenario.save_random");
  secfile_insert_bool(saving->file, game.scenario.players, "scenario.players");
  secfile_insert_bool(saving->file, game.scenario.startpos_nations,
                      "scenario.startpos_nations");
  if (game.scenario.prevent_new_cities) {
    secfile_insert_bool(saving->file, game.scenario.prevent_new_cities,
                        "scenario.prevent_new_cities");
  }
  secfile_insert_bool(saving->file, game.scenario.lake_flooding,
                      "scenario.lake_flooding");
  if (game.scenario.handmade) {
    secfile_insert_bool(saving->file, game.scenario.handmade,
                        "scenario.handmade");
  }
  if (game.scenario.allow_ai_type_fallback) {
    secfile_insert_bool(saving->file, game.scenario.allow_ai_type_fallback,
                        "scenario.allow_ai_type_fallback");
  }

  if (game.scenario.datafile[0] != '\0') {
    secfile_insert_str(saving->file, game.scenario.datafile,
                       "scenario.datafile");
  }
  secfile_insert_bool(saving->file, game.scenario.ruleset_locked,
                      "scenario.ruleset_locked");
  if (!game.scenario.ruleset_locked && game.scenario.req_caps[0] != '\0') {
    secfile_insert_str(saving->file, game.scenario.req_caps,
                       "scenario.ruleset_caps");
  }
}

/* =======================================================================
 * Load / save game settings.
 * ======================================================================= */

/************************************************************************//**
  Load '[settings]'.
****************************************************************************/
static void sg_load_settings(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  settings_game_load(loading->file, "settings");

  /* Save current status of fogofwar. */
  game.server.fogofwar_old = game.info.fogofwar;

  /* Add all compatibility settings here. */
}

/************************************************************************//**
  Save [settings].
****************************************************************************/
static void sg_save_settings(struct savedata *saving)
{
  enum map_generator real_generator = wld.map.server.generator;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  if (saving->scenario) {
    wld.map.server.generator = MAPGEN_SCENARIO; /* We want a scenario. */
  }

  settings_game_save(saving->file, "settings");
  /* Restore real map generator. */
  wld.map.server.generator = real_generator;

  /* Add all compatibility settings here. */
}


/************************************************************************//**
Load '[counters]'.
****************************************************************************/
static void sg_load_counters(struct loaddata *loading)
{
  struct city *pcity;
  int i, j;
  size_t length, length2;
  int *city_count;
  length = secfile_lookup_int_default(loading->file, 0,
                              "savefile.city_counters_order_size");

  if (0==length) {

    return;
  }

  loading->counter.order = secfile_lookup_str_vec(loading->file, &loading->counter.size, "savefile.city_counters_order_vector");

  sg_failure_ret(loading->counter.order != 0,
                   "Failed to load counter's ruleset order: %s",
                   secfile_error());
  sg_failure_ret(loading->counter.size = length,
                   "Counter vector in savegame have bad size: %s",
                   secfile_error());

  int corder[length];

  for (i = 0; i < length; i++) {

    struct counter *ctg = counter_by_rule_name(loading->counter.order[i]);
    corder[i] = counter_index(ctg);
  }

  i = 0;
  while (NULL != (city_count =
    secfile_lookup_int_vec(loading->file, &length2,
                           "counters.c%d", i))) {

    sg_failure_ret((length2 -1 == (size_t) length), ( "Bad city counters vector size. Should be " SIZE_T_PRINTF ". Is " SIZE_T_PRINTF "." ), length, length2 - 1);

    pcity = game_city_by_number(city_count[0]);

    sg_failure_ret(NULL != pcity, "City with id %d not found. Is savegame malformed? Abort loading.", city_count[0]);

    for (j = 0; j < length; j++) {

      pcity->counter_values[corder[j]] = city_count[j+1];
    }
    i++;
  }
}

/************************************************************************//**
  Save [counters].
****************************************************************************/
static void sg_save_counters(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  const char **countnames;
  int         *countvalues;
  int    i, j, count;

  count = counters_get_city_counters_count();

  secfile_insert_int(saving->file, count,
                     "savefile.city_counters_order_size");

  if (0 == count) {

    return;
  }

  countnames = fc_calloc(count, sizeof(*countnames));
  for (j = 0; j < count; j++) {
    countnames[j] = counter_rule_name(counter_by_index(j, CTGT_CITY));
  }

  secfile_insert_str_vec(saving->file, countnames, count,
                         "savefile.city_counters_order_vector");

  free(countnames);

  // Saving city's counters

  j = 0;
  countvalues = fc_calloc(count+1, sizeof(*countvalues));

  players_iterate(_pplayer) {

    city_list_iterate(_pplayer->cities, pcity) {

      countvalues[0] = pcity->id;
      for (i = 0; i < count; ++i) {

        countvalues[i+1] = pcity->counter_values[i];
      }

      secfile_insert_int_vec(saving->file, countvalues, count + 1, "counters.c%d", j);
      ++j;
    } city_list_iterate_end;
  } players_iterate_end;

  free(countvalues);
}

/* =======================================================================
 * Load / save the main map.
 * ======================================================================= */

/************************************************************************//**
  Load '[map'].
****************************************************************************/
static void sg_load_map(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  /* This defaults to TRUE even if map has not been generated.
   * We rely on that
   *   1) scenario maps have it explicitly right.
   *   2) when map is actually generated, it re-initialize this to FALSE. */
  wld.map.server.have_huts
    = secfile_lookup_bool_default(loading->file, TRUE, "map.have_huts");

  sg_failure_ret(secfile_lookup_bool(loading->file, &wld.map.altitude_info,
                                     "map.altitude"),
                 "%s", secfile_error());

  game.scenario.have_resources
    = secfile_lookup_bool_default(loading->file, TRUE, "map.have_resources");

  wld.map.server.have_resources = game.scenario.have_resources;

  /* Savegame may have stored random_seed for documentation purposes only,
   * but we want to keep it for resaving. */
  wld.map.server.seed
    = secfile_lookup_int_default(loading->file, 0, "map.random_seed");

  if (S_S_INITIAL == loading->server_state
      && MAPGEN_SCENARIO == wld.map.server.generator) {
    /* Generator MAPGEN_SCENARIO is used;
     * this map was done with the map editor. */

    /* Load tiles. */
    sg_load_map_tiles(loading);
    sg_load_map_startpos(loading);
    sg_load_map_tiles_extras(loading);

    /* Nothing more needed for a scenario. */
    secfile_entry_ignore(loading->file, "game.save_known");

    return;
  }

  if (S_S_INITIAL == loading->server_state) {
    /* Nothing more to do if it is not a scenario but in initial state. */
    return;
  }

  sg_load_map_tiles(loading);
  if (wld.map.altitude_info) {
    sg_load_map_altitude(loading);
  }
  sg_load_map_startpos(loading);
  sg_load_map_tiles_extras(loading);
  sg_load_map_known(loading);
  sg_load_map_owner(loading);
  sg_load_map_worked(loading);
}

/************************************************************************//**
  Save 'map'.
****************************************************************************/
static void sg_save_map(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  if (map_is_empty()) {
    /* No map. */
    return;
  }

  if (saving->scenario) {
    secfile_insert_bool(saving->file, wld.map.server.have_huts,
                        "map.have_huts");
    secfile_insert_bool(saving->file, game.scenario.have_resources,
                        "map.have_resources");
  } else {
    secfile_insert_bool(saving->file, TRUE, "map.have_huts");
    secfile_insert_bool(saving->file, TRUE, "map.have_resources");
  }

  secfile_insert_bool(saving->file, wld.map.altitude_info, "map.altitude");

  /* For debugging purposes only.
   * Do not save it if it's 0 (not known);
   * this confuses people reading this 'document' less than
   * saving 0. */
  if (wld.map.server.seed != 0) {
    secfile_insert_int(saving->file, wld.map.server.seed,
                       "map.random_seed");
  }

  sg_save_map_tiles(saving);
  if (wld.map.altitude_info) {
    sg_save_map_altitude(saving);
  }
  sg_save_map_startpos(saving);
  sg_save_map_tiles_extras(saving);
  sg_save_map_owner(saving);
  sg_save_map_worked(saving);
  sg_save_map_known(saving);
}

/************************************************************************//**
  Load tiles of the map.
****************************************************************************/
static void sg_load_map_tiles(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  /* Initialize the map for the current topology. 'map.xsize' and
   * 'map.ysize' must be set. */
  map_init_topology(&(wld.map));

  /* Allocate map. */
  main_map_allocate();

  /* get the terrain type */
  LOAD_MAP_CHAR(ch, ptile, ptile->terrain = char2terrain(ch), loading->file,
                "map.t%04d");
  assign_continent_numbers();

  /* Check for special tile sprites. */
  whole_map_iterate(&(wld.map), ptile) {
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

/************************************************************************//**
  Save all map tiles
****************************************************************************/
static void sg_save_map_tiles(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  /* Save the terrain type. */
  SAVE_MAP_CHAR(ptile, terrain2char(ptile->terrain), saving->file,
                "map.t%04d");

  /* Save special tile sprites. */
  whole_map_iterate(&(wld.map), ptile) {
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

/************************************************************************//**
  Load map tiles altitude
****************************************************************************/
static void sg_load_map_altitude(struct loaddata *loading)
{
  int y;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  for (y = 0; y < MAP_NATIVE_HEIGHT; y++) {
    const char *buffer = secfile_lookup_str(loading->file,
                                            "map.alt%04d", y);
    const char *ptr = buffer;
    int x;

    sg_failure_ret(buffer != nullptr, "%s", secfile_error());

    for (x = 0; x < MAP_NATIVE_WIDTH; x++) {
      char token[TOKEN_SIZE];
      struct tile *ptile = native_pos_to_tile(&(wld.map), x, y);
      int number;

      scanin(&ptr, ",", token, sizeof(token));
      sg_failure_ret(token[0] != '\0',
                     "Map size not correct (map.alt%d).", y);
      sg_failure_ret(str_to_int(token, &number),
                     "Got map alt %s in (%d, %d).", token, x, y);
      ptile->altitude = number;
    }
  }
}

/************************************************************************//**
  Save map tiles altitude
****************************************************************************/
static void sg_save_map_altitude(struct savedata *saving)
{
  int y;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  for (y = 0; y < MAP_NATIVE_HEIGHT; y++) {
    char line[MAP_NATIVE_WIDTH * TOKEN_SIZE];
    int x;

    line[0] = '\0';
    for (x = 0; x < MAP_NATIVE_WIDTH; x++) {
      char token[TOKEN_SIZE];
      struct tile *ptile = native_pos_to_tile(&(wld.map), x, y);

      fc_snprintf(token, sizeof(token), "%d", ptile->altitude);

      strcat(line, token);
      if (x + 1 < MAP_NATIVE_WIDTH) {
        strcat(line, ",");
      }
    }

    secfile_insert_str(saving->file, line, "map.alt%04d", y);
  }
}

/************************************************************************//**
  Load extras to map
****************************************************************************/
static void sg_load_map_tiles_extras(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  /* Load extras. */
  halfbyte_iterate_extras(j, loading->extra.size) {
    LOAD_MAP_CHAR(ch, ptile, sg_extras_set_bv(&ptile->extras, ch,
                                              loading->extra.order + 4 * j),
                  loading->file, "map.e%02d_%04d", j);
  } halfbyte_iterate_extras_end;

  if (S_S_INITIAL != loading->server_state
      || MAPGEN_SCENARIO != wld.map.server.generator
      || game.scenario.have_resources) {
    whole_map_iterate(&(wld.map), ptile) {
      extra_type_by_cause_iterate(EC_RESOURCE, pres) {
        if (tile_has_extra(ptile, pres)) {
          tile_set_resource(ptile, pres);

          if (!terrain_has_resource(ptile->terrain, ptile->resource)) {
            BV_CLR(ptile->extras, extra_index(pres));
          }
        }
      } extra_type_by_cause_iterate_end;
    } whole_map_iterate_end;
  }
}

/************************************************************************//**
  Save information about extras on map
****************************************************************************/
static void sg_save_map_tiles_extras(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  /* Save extras. */
  halfbyte_iterate_extras(j, game.control.num_extra_types) {
    int mod[4];
    int l;

    for (l = 0; l < 4; l++) {
      if (4 * j + 1 > game.control.num_extra_types) {
        mod[l] = -1;
      } else {
        mod[l] = 4 * j + l;
      }
    }
    SAVE_MAP_CHAR(ptile, sg_extras_get_bv(ptile->extras, ptile->resource, mod),
                  saving->file, "map.e%02d_%04d", j);
  } halfbyte_iterate_extras_end;
}

/************************************************************************//**
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

  /* Check status and return if not OK (sg_success FALSE). */
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

    ptile = native_pos_to_tile(&(wld.map), nat_x, nat_y);
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
      && loading->server_state == S_S_INITIAL
      && map_startpos_count() < game.server.max_players) {
    log_verbose("Number of starts (%d) are lower than rules.max_players "
                "(%d), lowering rules.max_players.",
                map_startpos_count(), game.server.max_players);
    game.server.max_players = map_startpos_count();
  }

  /* Re-initialize nation availability in light of start positions.
   * This has to be after loading [scenario] and [map].startpos and
   * before we seek nations for players. */
  update_nations_with_startpos();
}

/************************************************************************//**
  Save the map start positions.
****************************************************************************/
static void sg_save_map_startpos(struct savedata *saving)
{
  struct tile *ptile;
  const char SEPARATOR = '#';
  int i = 0;

  /* Check status and return if not OK (sg_success FALSE). */
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

/************************************************************************//**
  Load tile owner information
****************************************************************************/
static void sg_load_map_owner(struct loaddata *loading)
{
  int y;
  struct tile *claimer = NULL;
  struct extra_type *placing = NULL;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  if (game.info.is_new_game) {
    /* No owner/source information for a new game / scenario. */
    return;
  }

  /* Owner, ownership source, and infra turns are stored as plain numbers */
  for (y = 0; y < MAP_NATIVE_HEIGHT; y++) {
    const char *buffer1 = secfile_lookup_str(loading->file,
                                             "map.owner%04d", y);
    const char *buffer2 = secfile_lookup_str(loading->file,
                                             "map.source%04d", y);
    const char *buffer3 = secfile_lookup_str(loading->file,
                                             "map.eowner%04d", y);
    const char *buffer_placing = secfile_lookup_str_default(loading->file,
                                                            NULL,
                                                            "map.placing%04d", y);
    const char *buffer_turns = secfile_lookup_str_default(loading->file,
                                                          NULL,
                                                          "map.infra_turns%04d", y);
    const char *ptr1 = buffer1;
    const char *ptr2 = buffer2;
    const char *ptr3 = buffer3;
    const char *ptr_placing = buffer_placing;
    const char *ptr_turns = buffer_turns;
    int x;

    sg_failure_ret(buffer1 != NULL, "%s", secfile_error());
    sg_failure_ret(buffer2 != NULL, "%s", secfile_error());
    sg_failure_ret(buffer3 != NULL, "%s", secfile_error());

    for (x = 0; x < MAP_NATIVE_WIDTH; x++) {
      char token1[TOKEN_SIZE];
      char token2[TOKEN_SIZE];
      char token3[TOKEN_SIZE];
      char token_placing[TOKEN_SIZE];
      char token_turns[TOKEN_SIZE];
      struct player *owner = NULL;
      struct player *eowner = NULL;
      int turns;
      int number;
      struct tile *ptile = native_pos_to_tile(&(wld.map), x, y);

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
        claimer = index_to_tile(&(wld.map), number);
      }

      scanin(&ptr3, ",", token3, sizeof(token3));
      sg_failure_ret(token3[0] != '\0',
                     "Map size not correct (map.eowner%d).", y);
      if (strcmp(token3, "-") == 0) {
        eowner = NULL;
      } else {
        sg_failure_ret(str_to_int(token3, &number),
                       "Got base owner %s in (%d, %d).", token3, x, y);
        eowner = player_by_number(number);
      }

      if (ptr_placing != NULL) {
        scanin(&ptr_placing, ",", token_placing, sizeof(token_placing));
        sg_failure_ret(token_placing[0] != '\0',
                       "Map size not correct (map.placing%d).", y);
        if (strcmp(token_placing, "-") == 0) {
          placing = NULL;
        } else {
          sg_failure_ret(str_to_int(token_placing, &number),
                         "Got placing extra %s in (%d, %d).", token_placing, x, y);
          placing = extra_by_number(number);
        }
      } else {
        placing = NULL;
      }

      if (ptr_turns != NULL) {
        scanin(&ptr_turns, ",", token_turns, sizeof(token_turns));
        sg_failure_ret(token_turns[0] != '\0',
                       "Map size not correct (map.infra_turns%d).", y);
        sg_failure_ret(str_to_int(token_turns, &number),
                       "Got infra_turns %s in (%d, %d).", token_turns, x, y);
        turns = number;
      } else {
        turns = 1;
      }

      map_claim_ownership(ptile, owner, claimer, FALSE);
      tile_claim_bases(ptile, eowner);
      ptile->placing = placing;
      ptile->infra_turns = turns;
      log_debug("extras_owner(%d, %d) = %s", TILE_XY(ptile), player_name(eowner));
    }
  }
}

/************************************************************************//**
  Save tile owner information
****************************************************************************/
static void sg_save_map_owner(struct savedata *saving)
{
  int y;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  if (saving->scenario && !saving->save_players) {
    /* Nothing to do for a scenario without saved players. */
    return;
  }

  /* Store owner and ownership source as plain numbers. */
  for (y = 0; y < MAP_NATIVE_HEIGHT; y++) {
    char line[MAP_NATIVE_WIDTH * TOKEN_SIZE];
    int x;

    line[0] = '\0';
    for (x = 0; x < MAP_NATIVE_WIDTH; x++) {
      char token[TOKEN_SIZE];
      struct tile *ptile = native_pos_to_tile(&(wld.map), x, y);

      if (!saving->save_players || tile_owner(ptile) == NULL) {
        strcpy(token, "-");
      } else {
        fc_snprintf(token, sizeof(token), "%d",
                    player_number(tile_owner(ptile)));
      }
      strcat(line, token);
      if (x + 1 < MAP_NATIVE_WIDTH) {
        strcat(line, ",");
      }
    }

    secfile_insert_str(saving->file, line, "map.owner%04d", y);
  }

  for (y = 0; y < MAP_NATIVE_HEIGHT; y++) {
    char line[MAP_NATIVE_WIDTH * TOKEN_SIZE];
    int x;

    line[0] = '\0';
    for (x = 0; x < MAP_NATIVE_WIDTH; x++) {
      char token[TOKEN_SIZE];
      struct tile *ptile = native_pos_to_tile(&(wld.map), x, y);

      if (ptile->claimer == NULL) {
        strcpy(token, "-");
      } else {
        fc_snprintf(token, sizeof(token), "%d", tile_index(ptile->claimer));
      }
      strcat(line, token);
      if (x + 1 < MAP_NATIVE_WIDTH) {
        strcat(line, ",");
      }
    }

    secfile_insert_str(saving->file, line, "map.source%04d", y);
  }

  for (y = 0; y < MAP_NATIVE_HEIGHT; y++) {
    char line[MAP_NATIVE_WIDTH * TOKEN_SIZE];
    int x;

    line[0] = '\0';
    for (x = 0; x < MAP_NATIVE_WIDTH; x++) {
      char token[TOKEN_SIZE];
      struct tile *ptile = native_pos_to_tile(&(wld.map), x, y);

      if (!saving->save_players || extra_owner(ptile) == NULL) {
        strcpy(token, "-");
      } else {
        fc_snprintf(token, sizeof(token), "%d",
                    player_number(extra_owner(ptile)));
      }
      strcat(line, token);
      if (x + 1 < MAP_NATIVE_WIDTH) {
        strcat(line, ",");
      }
    }

    secfile_insert_str(saving->file, line, "map.eowner%04d", y);
  }

  for (y = 0; y < MAP_NATIVE_HEIGHT; y++) {
    char line[MAP_NATIVE_WIDTH * TOKEN_SIZE];
    int x;

    line[0] = '\0';
    for (x = 0; x < MAP_NATIVE_WIDTH; x++) {
      char token[TOKEN_SIZE];
      struct tile *ptile = native_pos_to_tile(&(wld.map), x, y);

      if (ptile->placing == NULL) {
        strcpy(token, "-");
      } else {
        fc_snprintf(token, sizeof(token), "%d",
                    extra_number(ptile->placing));
      }
      strcat(line, token);
      if (x + 1 < MAP_NATIVE_WIDTH) {
        strcat(line, ",");
      }
    }

    secfile_insert_str(saving->file, line, "map.placing%04d", y);
  }

  for (y = 0; y < MAP_NATIVE_HEIGHT; y++) {
    char line[MAP_NATIVE_WIDTH * TOKEN_SIZE];
    int x;

    line[0] = '\0';
    for (x = 0; x < MAP_NATIVE_WIDTH; x++) {
      char token[TOKEN_SIZE];
      struct tile *ptile = native_pos_to_tile(&(wld.map), x, y);

      if (ptile->placing != NULL) {
        fc_snprintf(token, sizeof(token), "%d",
                    ptile->infra_turns);
      } else {
        fc_snprintf(token, sizeof(token), "0"); 
      }
      strcat(line, token);
      if (x + 1 < MAP_NATIVE_WIDTH) {
        strcat(line, ",");
      }
    }

    secfile_insert_str(saving->file, line, "map.infra_turns%04d", y);
  }
}

/************************************************************************//**
  Load worked tiles information
****************************************************************************/
static void sg_load_map_worked(struct loaddata *loading)
{
  int x, y;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  sg_failure_ret(loading->worked_tiles == NULL,
                 "City worked map not loaded!");

  loading->worked_tiles = fc_malloc(MAP_INDEX_SIZE *
                                    sizeof(*loading->worked_tiles));

  for (y = 0; y < MAP_NATIVE_HEIGHT; y++) {
    const char *buffer = secfile_lookup_str(loading->file, "map.worked%04d",
                                            y);
    const char *ptr = buffer;

    sg_failure_ret(NULL != buffer,
                   "Savegame corrupt - map line %d not found.", y);
    for (x = 0; x < MAP_NATIVE_WIDTH; x++) {
      char token[TOKEN_SIZE];
      int number;
      struct tile *ptile = native_pos_to_tile(&(wld.map), x, y);

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

/************************************************************************//**
  Save worked tiles information
****************************************************************************/
static void sg_save_map_worked(struct savedata *saving)
{
  int x, y;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  if (saving->scenario && !saving->save_players) {
    /* Nothing to do for a scenario without saved players. */
    return;
  }

  /* Additionally save the tiles worked by the cities */
  for (y = 0; y < MAP_NATIVE_HEIGHT; y++) {
    char line[MAP_NATIVE_WIDTH * TOKEN_SIZE];

    line[0] = '\0';
    for (x = 0; x < MAP_NATIVE_WIDTH; x++) {
      char token[TOKEN_SIZE];
      struct tile *ptile = native_pos_to_tile(&(wld.map), x, y);
      struct city *pcity = tile_worked(ptile);

      if (pcity == NULL) {
        strcpy(token, "-");
      } else {
        fc_snprintf(token, sizeof(token), "%d", pcity->id);
      }
      strcat(line, token);
      if (x < MAP_NATIVE_WIDTH) {
        strcat(line, ",");
      }
    }
    secfile_insert_str(saving->file, line, "map.worked%04d", y);
  }
}

/************************************************************************//**
  Load tile known status
****************************************************************************/
static void sg_load_map_known(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  players_iterate(pplayer) {
    /* Allocate player private map here; it is needed in different modules
     * besides this one ((i.e. sg_load_player_*()). */
    player_map_init(pplayer);
  } players_iterate_end;

  if (secfile_lookup_bool_default(loading->file, TRUE,
                                  "game.save_known")) {
    int lines = player_slot_max_used_number() / 32 + 1;
    int j, p, l, i;
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
    whole_map_iterate(&(wld.map), ptile) {
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

/************************************************************************//**
  Save tile known status for whole map and all players
****************************************************************************/
static void sg_save_map_known(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  if (!saving->save_players) {
    secfile_insert_bool(saving->file, FALSE, "game.save_known");
    return;
  } else {
    int lines = player_slot_max_used_number() / 32 + 1;

    secfile_insert_bool(saving->file, game.server.save_options.save_known,
                        "game.save_known");
    if (game.server.save_options.save_known) {
      int j, p, l, i;
      unsigned int *known = fc_calloc(lines * MAP_INDEX_SIZE, sizeof(*known));

      /* HACK: we convert the data into a 32-bit integer, and then save it as
       * hex. */

      whole_map_iterate(&(wld.map), ptile) {
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
 * This is split into two parts as some data can only be loaded if the
 * number of players is known and the corresponding player slots are
 * defined.
 * ======================================================================= */

/************************************************************************//**
  Load '[player]' (basic data).
****************************************************************************/
static void sg_load_players_basic(struct loaddata *loading)
{
  int i, k, nplayers;
  const char *str;
  bool shuffle_loaded = TRUE;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  if (S_S_INITIAL == loading->server_state
      || game.info.is_new_game) {
    /* Nothing more to do. */
    return;
  }

  /* Load destroyed wonders: */
  str = secfile_lookup_str(loading->file,
                           "players.destroyed_wonders");
  sg_failure_ret(str != NULL, "%s", secfile_error());
  sg_failure_ret(strlen(str) == loading->improvement.size,
                 "Invalid length for 'players.destroyed_wonders' ("
                 SIZE_T_PRINTF " ~= " SIZE_T_PRINTF ")",
                 strlen(str), loading->improvement.size);
  for (k = 0; k < loading->improvement.size; k++) {
    sg_failure_ret(str[k] == '1' || str[k] == '0',
                   "Undefined value '%c' within "
                   "'players.destroyed_wonders'.", str[k]);

    if (str[k] == '1') {
      struct impr_type *pimprove
        = improvement_by_rule_name(loading->improvement.order[k]);

      if (pimprove != NULL) {
        game.info.great_wonder_owners[improvement_index(pimprove)]
          = WONDER_DESTROYED;
      }
    }
  }

  server.identity_number
    = secfile_lookup_int_default(loading->file, server.identity_number,
                                 "players.identity_number_used");

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
    str = secfile_lookup_str(loading->file, "player%d.ai_type",
                             player_slot_index(pslot));
    sg_failure_ret(str != NULL, "%s", secfile_error());

    /* Get player color */
    if (!rgbcolor_load(loading->file, &prgbcolor, "player%d.color",
                       pslot_id)) {
      if (game_was_started()) {
        log_sg("Game has started, yet player %d has no color defined.",
               pslot_id);
        /* This will be fixed up later */
      } else {
        log_verbose("No color defined for player %d.", pslot_id);
        /* Colors will be assigned on game start, or at end of savefile
         * loading if game has already started */
      }
    }

    /* Create player. */
    pplayer = server_create_player(player_slot_index(pslot), str,
                                   prgbcolor,
                                   !game.scenario.is_scenario
                                   || game.scenario.allow_ai_type_fallback);
    sg_failure_ret(pplayer != NULL, "Invalid AI type: '%s'!", str);

    server_player_init(pplayer, FALSE, FALSE);

    /* Free the color definition. */
    rgbcolor_destroy(prgbcolor);

    /* Multipliers (policies) */

    /* First initialise player values with ruleset defaults; this will
     * cover any in the ruleset not known when the savefile was created. */
    multipliers_iterate(pmul) {
      pplayer->multipliers[multiplier_index(pmul)].value
        = pplayer->multipliers[multiplier_index(pmul)].target = pmul->def;
    } multipliers_iterate_end;

    /* Now override with any values from the savefile. */
    for (k = 0; k < loading->multiplier.size; k++) {
      const struct multiplier *pmul = loading->multiplier.order[k];

      if (pmul) {
        Multiplier_type_id idx = multiplier_index(pmul);
        int val =
          secfile_lookup_int_default(loading->file, pmul->def,
                                     "player%d.multiplier%d.val",
                                     player_slot_index(pslot), k);
        int rval = (((CLIP(pmul->start, val, pmul->stop)
                      - pmul->start) / pmul->step) * pmul->step) + pmul->start;

        if (rval != val) {
          log_verbose("Player %d had illegal value for multiplier \"%s\": "
                      "was %d, clamped to %d", pslot_id,
                      multiplier_rule_name(pmul), val, rval);
        }
        pplayer->multipliers[idx].value = rval;

        val =
          secfile_lookup_int_default(loading->file,
                                     pplayer->multipliers[idx].value,
                                     "player%d.multiplier%d.target",
                                     player_slot_index(pslot), k);
        rval = (((CLIP(pmul->start, val, pmul->stop)
                  - pmul->start) / pmul->step) * pmul->step) + pmul->start;

        if (rval != val) {
          log_verbose("Player %d had illegal value for multiplier_target "
                      " \"%s\": was %d, clamped to %d", pslot_id,
                      multiplier_rule_name(pmul), val, rval);
        }
        pplayer->multipliers[idx].target = rval;

        pplayer->multipliers[idx].changed
          = secfile_lookup_int_default(loading->file, 0,
                                       "player%d.multiplier%d.changed",
                                       player_slot_index(pslot), k);
      } /* else silently discard multiplier not in current ruleset */
    }

    /* Must be loaded before tile owner is set. */
    pplayer->server.border_vision =
        secfile_lookup_bool_default(loading->file, FALSE,
                                    "player%d.border_vision",
                                    player_slot_index(pslot));

    pplayer->autoselect_weight = secfile_lookup_int_default(loading->file, 0,
                                                            "player%d.autoselect_weight",
                                                            pslot_id);
  } player_slots_iterate_end;

  /* check number of players */
  nplayers = secfile_lookup_int_default(loading->file, 0, "players.nplayers");
  sg_failure_ret(player_count() == nplayers, "The value of players.nplayers "
                 "(%d) from the loaded game does not match the number of "
                 "players present (%d).", nplayers, player_count());

  /* Load team information. */
  players_iterate(pplayer) {
    int team;
    struct team_slot *tslot = NULL;

    sg_failure_ret(secfile_lookup_int(loading->file, &team,
                                      "player%d.team_no",
                                      player_number(pplayer))
                   && (tslot = team_slot_by_number(team)),
                   "Invalid team definition for player %s (nb %d).",
                   player_name(pplayer), player_number(pplayer));
    /* Should never fail when slot given is not nullptr */
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
    int slots = player_slot_count();
    int plrcount = player_count();
    int shuffled_players[slots];
    bool shuffled_player_set[slots];

    for (i = 0; i < slots; i++) {
      /* Array to save used numbers. */
      shuffled_player_set[i] = FALSE;
      /* List of all player IDs (needed for set_shuffled_players()). It is
       * initialised with the value -1 to indicate that no value is set. */
      shuffled_players[i] = -1;
    }

    /* Load shuffled player list. */
    for (i = 0; i < plrcount; i++) {
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
      int shuffle_index = plrcount;

      for (i = 0; i < slots; i++) {
        if (!shuffled_player_set[i]) {
          shuffled_players[shuffle_index++] = i;
        }

        /* shuffle_index must not grow higher than size of shuffled_players. */
        sg_failure_ret(shuffle_index <= slots,
                       "Invalid player shuffle data!");
      }

#ifdef FREECIV_DEBUG
      log_debug("[load shuffle] player_count() = %d", player_count());
      player_slots_iterate(pslot) {
        int plrid = player_slot_index(pslot);

        log_debug("[load shuffle] id: %3d => slot: %3d | slot %3d: %s",
                  plrid, shuffled_players[plrid], plrid,
                  shuffled_player_set[plrid] ? "is used" : "-");
      } player_slots_iterate_end;
#endif /* FREECIV_DEBUG */

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

/************************************************************************//**
  Load '[player]'.
****************************************************************************/
static void sg_load_players(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success FALSE). */
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

    /* Check the success of the functions above. */
    sg_check_ret();

    /* Print out some information */
    if (is_ai(pplayer)) {
      log_normal(_("%s has been added as %s level AI-controlled player "
                   "(%s)."), player_name(pplayer),
                 ai_level_translated_name(pplayer->ai_common.skill_level),
                 ai_name(pplayer->ai));
    } else {
      log_normal(_("%s has been added as human player."),
                 player_name(pplayer));
    }
  } players_iterate_end;

  /* Also load the transport status of the units here. It must be a special
   * case as all units must be known (unit on an allied transporter). */
  players_iterate(pplayer) {
    /* Load unit transport status. */
    sg_load_player_units_transport(loading, pplayer);
  } players_iterate_end;

  /* Savegame may contain nation assignments that are incompatible with the
   * current nationset. Ensure they are compatible, one way or another. */
  fit_nationset_to_players();

  /* Some players may have invalid nations in the ruleset. Once all players
   * are loaded, pick one of the remaining nations for them. */
  players_iterate(pplayer) {
    if (pplayer->nation == NO_NATION_SELECTED) {
      player_set_nation(pplayer, pick_a_nation(NULL, FALSE, TRUE,
                                               NOT_A_BARBARIAN));
      /* TRANS: Minor error message: <Leader> ... <Poles>. */
      log_sg(_("%s had invalid nation; changing to %s."),
             player_name(pplayer), nation_plural_for_player(pplayer));

      ai_traits_init(pplayer);
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

  /* Update cached city illness. This can depend on trade routes,
   * so can't be calculated until all players have been loaded. */
  if (game.info.illness_on) {
    cities_iterate(pcity) {
      pcity->server.illness
        = city_illness_calc(pcity, NULL, NULL,
                            &(pcity->illness_trade), NULL);
    } cities_iterate_end;
  }

  /* Update all city information. This must come after all cities are
   * loaded (in player_load) but before player (dumb) cities are loaded
   * in player_load_vision(). */
  players_iterate(plr) {
    city_list_iterate(plr->cities, pcity) {
      city_refresh(pcity);
      sanity_check_city(pcity);
      CALL_PLR_AI_FUNC(city_got, plr, plr, pcity);
    } city_list_iterate_end;
  } players_iterate_end;

  /* Since the cities must be placed on the map to put them on the
     player map we do this afterwards */
  players_iterate(pplayer) {
    sg_load_player_vision(loading, pplayer);
    /* Check the success of the function above. */
    sg_check_ret();
  } players_iterate_end;

  /* Check shared vision and tiles. */
  players_iterate(pplayer) {
    BV_CLR_ALL(pplayer->gives_shared_vision);
    BV_CLR_ALL(pplayer->gives_shared_tiles);
    BV_CLR_ALL(pplayer->server.really_gives_vision);
  } players_iterate_end;

  /* Set up shared vision... */
  players_iterate(pplayer) {
    int plr1 = player_index(pplayer);

    players_iterate(pplayer2) {
      int plr2 = player_index(pplayer2);

      if (secfile_lookup_bool_default(loading->file, FALSE,
              "player%d.diplstate%d.gives_shared_vision", plr1, plr2)) {
        give_shared_vision(pplayer, pplayer2);
      }
      if (secfile_lookup_bool_default(loading->file, FALSE,
              "player%d.diplstate%d.gives_shared_tiles", plr1, plr2)) {
        BV_SET(pplayer->gives_shared_tiles, player_index(pplayer2));
      }
    } players_iterate_end;
  } players_iterate_end;

  /* ...and check it */
  players_iterate(pplayer1) {
    players_iterate(pplayer2) {
      /* TODO: Is there a good reason player is not marked as
       *       giving shared vision to themselves -> really_gives_vision()
       *       returning FALSE when pplayer1 == pplayer2 */
      if (pplayer1 != pplayer2
          && players_on_same_team(pplayer1, pplayer2)) {
        if (!really_gives_vision(pplayer1, pplayer2)) {
          sg_regr(3000900,
                  _("%s did not give shared vision to team member %s."),
                  player_name(pplayer1), player_name(pplayer2));
          give_shared_vision(pplayer1, pplayer2);
        }
        if (!really_gives_vision(pplayer2, pplayer1)) {
          sg_regr(3000900,
                  _("%s did not give shared vision to team member %s."),
                  player_name(pplayer2), player_name(pplayer1));
          give_shared_vision(pplayer2, pplayer1);
        }
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
      if (!can_unit_continue_current_activity(&(wld.map), punit)) {
        struct tile *ptile = unit_tile(punit);

        log_sg("%s doing illegal activity in savegame!",
               unit_rule_name(punit));
        log_sg("Activity: %s, Target: %s, Tile: (%d, %d), Terrain: %s",
               unit_activity_name(punit->activity),
               punit->activity_target ? extra_rule_name(
                                          punit->activity_target)
                                      : "missing",
               TILE_XY(ptile), terrain_rule_name(tile_terrain(ptile)));
        punit->activity = ACTIVITY_IDLE;
      }
    } unit_list_iterate_end;
  } players_iterate_end;

  cities_iterate(pcity) {
    city_refresh(pcity);
    city_thaw_workers(pcity); /* may auto_arrange_workers() */
  } cities_iterate_end;

  /* Player colors are always needed once game has started. Pre-2.4 savegames
   * lack them. This cannot be in compatibility conversion layer as we need
   * all the player data available to be able to assign best colors. */
  if (game_was_started()) {
    assign_player_colors();
  }
}

/************************************************************************//**
  Save '[player]'.
****************************************************************************/
static void sg_save_players(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success FALSE). */
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

/************************************************************************//**
  Main player data loading function
****************************************************************************/
static void sg_load_player_main(struct loaddata *loading,
                                struct player *plr)
{
  const char **slist;
  int i, plrno = player_number(plr);
  const char *str;
  struct government *gov;
  const char *level;
  const char *barb_str;
  size_t nval;
  const char *kind;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  /* Basic player data. */
  str = secfile_lookup_str(loading->file, "player%d.name", plrno);
  sg_failure_ret(str != NULL, "%s", secfile_error());
  server_player_set_name(plr, str);
  sz_strlcpy(plr->username,
             secfile_lookup_str_default(loading->file, "",
                                        "player%d.username", plrno));
  sg_failure_ret(secfile_lookup_bool(loading->file, &plr->unassigned_user,
                                     "player%d.unassigned_user", plrno),
                 "%s", secfile_error());
  sz_strlcpy(plr->server.orig_username,
             secfile_lookup_str_default(loading->file, "",
                                        "player%d.orig_username", plrno));
  sz_strlcpy(plr->ranked_username,
             secfile_lookup_str_default(loading->file, "",
                                        "player%d.ranked_username",
                                        plrno));
  sg_failure_ret(secfile_lookup_bool(loading->file, &plr->unassigned_ranked,
                                     "player%d.unassigned_ranked", plrno),
                 "%s", secfile_error());
  str = secfile_lookup_str_default(loading->file, "",
                                   "player%d.delegation_username",
                                   plrno);
  /* Defaults to no delegation. */
  if (strlen(str)) {
    player_delegation_set(plr, str);
  }

  /* Player flags */
  BV_CLR_ALL(plr->flags);
  slist = secfile_lookup_str_vec(loading->file, &nval, "player%d.flags", plrno);
  for (i = 0; i < nval; i++) {
    const char *sval = slist[i];
    enum plr_flag_id fid = plr_flag_id_by_name(sval, fc_strcasecmp);

    sg_failure_ret(plr_flag_id_is_valid(fid), "Invalid player flag \"%s\".", sval);

    BV_SET(plr->flags, fid);
  }
  free(slist);

  /* Nation */
  str = secfile_lookup_str(loading->file, "player%d.nation", plrno);
  player_set_nation(plr, nation_by_rule_name(str));
  if (plr->nation != NULL) {
    ai_traits_init(plr);
  }

  /* Government */
  str = secfile_lookup_str(loading->file, "player%d.government_name",
                           plrno);
  gov = government_by_rule_name(str);
  sg_failure_ret(gov != NULL, "Player%d: unsupported government \"%s\".",
                 plrno, str);
  plr->government = gov;

  /* Target government */
  str = secfile_lookup_str(loading->file,
                           "player%d.target_government_name", plrno);
  if (str != NULL) {
    plr->target_government = government_by_rule_name(str);
  } else {
    plr->target_government = NULL;
  }
  plr->revolution_finishes
    = secfile_lookup_int_default(loading->file, -1,
                                 "player%d.revolution_finishes", plrno);

  /* Load diplomatic data (diplstate + embassy + vision).
   * Shared vision is loaded in sg_load_players(). */
  BV_CLR_ALL(plr->real_embassy);
  players_iterate(pplayer) {
    char buf[32];
    struct player_diplstate *ds = player_diplstate_get(plr, pplayer);
    i = player_index(pplayer);

    /* Load diplomatic status */
    fc_snprintf(buf, sizeof(buf), "player%d.diplstate%d", plrno, i);

    ds->type =
      secfile_lookup_enum_default(loading->file, DS_WAR,
                                  diplstate_type, "%s.current", buf);
    ds->max_state =
      secfile_lookup_enum_default(loading->file, DS_WAR,
                                  diplstate_type, "%s.closest", buf);

    /* FIXME: If either party is barbarian, we cannot enforce below check */
#if 0
    if (ds->type == DS_WAR && ds->first_contact_turn <= 0) {
      sg_regr(3020000,
              "Player%d: War with player %d who has never been met. "
              "Reverted to No Contact state.", plrno, i);
      ds->type = DS_NO_CONTACT;
    }
#endif

    if (valid_dst_closest(ds) != ds->max_state) {
      sg_regr(3020000,
              "Player%d: closest diplstate to player %d less than current. "
              "Updated.", plrno, i);
      ds->max_state = ds->type;
    }

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
    CALL_FUNC_EACH_AI(player_load_relations, plr, aplayer, loading->file, plrno);
  } players_iterate_end;

  plr->server.adv->wonder_city = secfile_lookup_int_default(loading->file, 0,
                                                            "player%d.adv.wonder_city",
                                                            plrno);

  CALL_FUNC_EACH_AI(player_load, plr, loading->file, plrno);

  /* Some sane defaults */
  plr->ai_common.fuzzy = 0;
  plr->ai_common.expand = 100;
  plr->ai_common.science_cost = 100;


  level = secfile_lookup_str_default(loading->file, NULL,
                                     "player%d.ai.level", plrno);
  if (level != NULL && !fc_strcasecmp("Handicapped", level)) {
    /* Up to freeciv-3.1 Restricted AI level was known as Handicapped */
    plr->ai_common.skill_level = AI_LEVEL_RESTRICTED;
  } else {
    plr->ai_common.skill_level = ai_level_by_name(level, fc_strcasecmp);
  }

  if (!ai_level_is_valid(plr->ai_common.skill_level)) {
    log_sg("Player%d: Invalid AI level \"%s\". "
           "Changed to \"%s\".", plrno, level,
           ai_level_name(game.info.skill_level));
    plr->ai_common.skill_level = game.info.skill_level;
  }

  barb_str = secfile_lookup_str_default(loading->file, "None",
                                        "player%d.ai.barb_type", plrno);
  plr->ai_common.barbarian_type = barbarian_type_by_name(barb_str, fc_strcasecmp);

  if (!barbarian_type_is_valid(plr->ai_common.barbarian_type)) {
    log_sg("Player%d: Invalid barbarian type \"%s\". "
           "Changed to \"None\".", plrno, barb_str);
    plr->ai_common.barbarian_type = NOT_A_BARBARIAN;
  }

  if (is_barbarian(plr)) {
    server.nbarbarians++;
  }

  if (is_ai(plr)) {
    set_ai_level_directer(plr, plr->ai_common.skill_level);
    CALL_PLR_AI_FUNC(gained_control, plr, plr);
  }

  /* Load nation style. */
  {
    struct nation_style *style;

    str = secfile_lookup_str(loading->file, "player%d.style_by_name", plrno);

    sg_failure_ret(str != NULL, "%s", secfile_error());
    style = style_by_rule_name(str);
    if (style == NULL) {
      style = style_by_number(0);
      log_sg("Player%d: unsupported city_style_name \"%s\". "
             "Changed to \"%s\".", plrno, str, style_rule_name(style));
    }
    plr->style = style;
  }

  sg_failure_ret(secfile_lookup_int(loading->file, &plr->nturns_idle,
                                    "player%d.idle_turns", plrno),
                 "%s", secfile_error());
  kind = secfile_lookup_str(loading->file, "player%d.kind", plrno);
  if (sex_by_name(kind) == SEX_MALE) {
    plr->is_male = TRUE;
  } else {
    plr->is_male = FALSE;
  }
  sg_failure_ret(secfile_lookup_bool(loading->file, &plr->is_alive,
                                     "player%d.is_alive", plrno),
                 "%s", secfile_error());
  sg_failure_ret(secfile_lookup_int(loading->file, &plr->turns_alive,
                                    "player%d.turns_alive", plrno),
                 "%s", secfile_error());
  sg_failure_ret(secfile_lookup_int(loading->file, &plr->last_war_action,
                                    "player%d.last_war", plrno),
                 "%s", secfile_error());
  plr->phase_done = secfile_lookup_bool_default(loading->file, FALSE,
                                                "player%d.phase_done", plrno);
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
  plr->economic.infra_points = secfile_lookup_int_default(loading->file, 0,
                                                          "player%d.infrapts",
                                                          plrno);
  plr->server.bulbs_last_turn =
    secfile_lookup_int_default(loading->file, 0,
                               "player%d.research.bulbs_last_turn", plrno);

  /* Traits */
  if (plr->nation) {
    for (i = 0; i < loading->trait.size; i++) {
      enum trait tr = trait_by_name(loading->trait.order[i], fc_strcasecmp);

      if (trait_is_valid(tr)) {
        int val;

        sg_failure_ret(secfile_lookup_int(loading->file, &val, "player%d.trait%d.val",
                                          plrno, i),
                       "%s", secfile_error());
        plr->ai_common.traits[tr].val = val;

        sg_failure_ret(secfile_lookup_int(loading->file, &val,
                                          "player%d.trait%d.mod", plrno, i),
                       "%s", secfile_error());
        plr->ai_common.traits[tr].mod = val;
      }
    }
  }

  /* Achievements */
  {
    int count;

    count = secfile_lookup_int_default(loading->file, -1,
                                       "player%d.achievement_count", plrno);

    if (count > 0) {
      for (i = 0; i < count; i++) {
        const char *name;
        struct achievement *pach;
        bool first;

        name = secfile_lookup_str(loading->file,
                                  "player%d.achievement%d.name", plrno, i);
        pach = achievement_by_rule_name(name);

        sg_failure_ret(pach != NULL,
                       "Unknown achievement \"%s\".", name);

        sg_failure_ret(secfile_lookup_bool(loading->file, &first,
                                           "player%d.achievement%d.first",
                                           plrno, i),
                       "achievement error: %s", secfile_error());

        sg_failure_ret(pach->first == NULL || !first,
                       "Multiple players listed as first to get achievement \"%s\".",
                       name);

        BV_SET(pach->achievers, player_index(plr));

        if (first) {
          pach->first = plr;
        }
      }
    }
  }

  /* Player score. */
  plr->score.happy =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.happy", plrno);
  plr->score.content =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.content", plrno);
  plr->score.unhappy =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.unhappy", plrno);
  plr->score.angry =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.angry", plrno);

  /* Make sure that the score about specialists in current ruleset that
   * were not present at saving time are set to zero. */
  specialist_type_iterate(sp) {
    plr->score.specialists[sp] = 0;
  } specialist_type_iterate_end;

  for (i = 0; i < loading->specialist.size; i++) {
    plr->score.specialists[specialist_index(loading->specialist.order[i])]
      = secfile_lookup_int_default(loading->file, 0,
                                   "score%d.specialists%d", plrno, i);
  }

  plr->score.wonders =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.wonders", plrno);
  plr->score.techs =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.techs", plrno);
  plr->score.techout =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.techout", plrno);
  plr->score.landarea =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.landarea", plrno);
  plr->score.settledarea =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.settledarea", plrno);
  plr->score.population =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.population", plrno);
  plr->score.cities =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.cities", plrno);
  plr->score.units =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.units", plrno);
  plr->score.pollution =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.pollution", plrno);
  plr->score.literacy =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.literacy", plrno);
  plr->score.bnp =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.bnp", plrno);
  plr->score.mfg =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.mfg", plrno);
  plr->score.spaceship =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.spaceship", plrno);
  plr->score.units_built =
      secfile_lookup_int_default(loading->file, 0,
                                 "score%d.units_built", plrno);
  plr->score.units_killed =
      secfile_lookup_int_default(loading->file, 0,
                                 "score%d.units_killed", plrno);
  plr->score.units_lost =
      secfile_lookup_int_default(loading->file, 0,
                                 "score%d.units_lost", plrno);
  plr->score.units_used =
      secfile_lookup_int_default(loading->file, 0,
                                 "score%d.units_used", plrno);
  plr->score.culture =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.culture", plrno);
  plr->score.game =
    secfile_lookup_int_default(loading->file, 0,
                               "score%d.total", plrno);

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

        if (!(st[i] == '0')) {
          BV_SET(ship->structure, i);
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
  str = secfile_lookup_str(loading->file, "player%d.lost_wonders", plrno);
  /* If not present, probably an old savegame; nothing to be done */
  if (str != NULL) {
    int k;

    sg_failure_ret(strlen(str) == loading->improvement.size,
                   "Invalid length for 'player%d.lost_wonders' ("
                   SIZE_T_PRINTF " ~= " SIZE_T_PRINTF ")",
                   plrno, strlen(str), loading->improvement.size);
    for (k = 0; k < loading->improvement.size; k++) {
      sg_failure_ret(str[k] == '1' || str[k] == '0',
                     "Undefined value '%c' within "
                     "'player%d.lost_wonders'.", plrno, str[k]);

      if (str[k] == '1') {
        struct impr_type *pimprove =
            improvement_by_rule_name(loading->improvement.order[k]);
        if (pimprove) {
          plr->wonders[improvement_index(pimprove)] = WONDER_LOST;
        }
      }
    }
  }

  plr->history =
    secfile_lookup_int_default(loading->file, 0, "player%d.history", plrno);
  plr->server.huts =
    secfile_lookup_int_default(loading->file, 0, "player%d.hut_count", plrno);
}

/************************************************************************//**
  Main player data saving function.
****************************************************************************/
static void sg_save_player_main(struct savedata *saving,
                                struct player *plr)
{
  int i, k, plrno = player_number(plr);
  struct player_spaceship *ship = &plr->spaceship;
  const char *flag_names[PLRF_COUNT];
  int set_count;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  set_count = 0;
  for (i = 0; i < PLRF_COUNT; i++) {
    if (player_has_flag(plr, i)) {
      flag_names[set_count++] = plr_flag_id_name(i);
    }
  }

  secfile_insert_str_vec(saving->file, flag_names, set_count,
                         "player%d.flags", plrno);

  secfile_insert_str(saving->file, ai_name(plr->ai),
                     "player%d.ai_type", plrno);
  secfile_insert_str(saving->file, player_name(plr),
                     "player%d.name", plrno);
  secfile_insert_str(saving->file, plr->username,
                     "player%d.username", plrno);
  secfile_insert_bool(saving->file, plr->unassigned_user,
                      "player%d.unassigned_user", plrno);
  if (plr->rgb != NULL) {
    rgbcolor_save(saving->file, plr->rgb, "player%d.color", plrno);
  } else {
    /* Colorless players are ok in pregame */
    if (game_was_started()) {
      log_sg("Game has started, yet player %d has no color defined.", plrno);
    }
  }
  secfile_insert_str(saving->file, plr->ranked_username,
                     "player%d.ranked_username", plrno);
  secfile_insert_bool(saving->file, plr->unassigned_ranked,
                      "player%d.unassigned_ranked", plrno);
  secfile_insert_str(saving->file, plr->server.orig_username,
                     "player%d.orig_username", plrno);
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

  secfile_insert_str(saving->file, style_rule_name(plr->style),
                      "player%d.style_by_name", plrno);

  secfile_insert_int(saving->file, plr->nturns_idle,
                     "player%d.idle_turns", plrno);
  if (plr->is_male) {
    secfile_insert_str(saving->file, sex_rule_name(SEX_MALE),
                       "player%d.kind", plrno);
  } else {
    secfile_insert_str(saving->file, sex_rule_name(SEX_FEMALE),
                       "player%d.kind", plrno);
  }
  secfile_insert_bool(saving->file, plr->is_alive,
                      "player%d.is_alive", plrno);
  secfile_insert_int(saving->file, plr->turns_alive,
                     "player%d.turns_alive", plrno);
  secfile_insert_int(saving->file, plr->last_war_action,
                     "player%d.last_war", plrno);
  secfile_insert_bool(saving->file, plr->phase_done,
                      "player%d.phase_done", plrno);

  players_iterate(pplayer) {
    char buf[32];
    struct player_diplstate *ds = player_diplstate_get(plr, pplayer);

    i = player_index(pplayer);

    /* save diplomatic state */
    fc_snprintf(buf, sizeof(buf), "player%d.diplstate%d", plrno, i);

    secfile_insert_enum(saving->file, ds->type,
                        diplstate_type, "%s.current", buf);
    secfile_insert_enum(saving->file, ds->max_state,
                        diplstate_type, "%s.closest", buf);
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
    secfile_insert_bool(saving->file, gives_shared_tiles(plr, pplayer),
                        "%s.gives_shared_tiles", buf);
  } players_iterate_end;

  players_iterate(aplayer) {
    i = player_index(aplayer);
    /* save ai data */
    secfile_insert_int(saving->file, plr->ai_common.love[i],
                       "player%d.ai%d.love", plrno, i);
    CALL_FUNC_EACH_AI(player_save_relations, plr, aplayer, saving->file, plrno);
  } players_iterate_end;

  secfile_insert_int(saving->file, plr->server.adv->wonder_city,
                     "player%d.adv.wonder_city", plrno);

  CALL_FUNC_EACH_AI(player_save, plr, saving->file, plrno);

  /* Multipliers (policies) */
  i = multiplier_count();

  for (k = 0; k < i; k++) {
    secfile_insert_int(saving->file, plr->multipliers[k].value,
                       "player%d.multiplier%d.val", plrno, k);
    secfile_insert_int(saving->file, plr->multipliers[k].target,
                       "player%d.multiplier%d.target", plrno, k);
    secfile_insert_int(saving->file, plr->multipliers[k].changed,
                       "player%d.multiplier%d.changed", plrno, k);
  }

  secfile_insert_str(saving->file, ai_level_name(plr->ai_common.skill_level),
                     "player%d.ai.level", plrno);
  secfile_insert_str(saving->file, barbarian_type_name(plr->ai_common.barbarian_type),
                     "player%d.ai.barb_type", plrno);
  secfile_insert_int(saving->file, plr->economic.gold,
                     "player%d.gold", plrno);
  secfile_insert_int(saving->file, plr->economic.tax,
                     "player%d.rates.tax", plrno);
  secfile_insert_int(saving->file, plr->economic.science,
                     "player%d.rates.science", plrno);
  secfile_insert_int(saving->file, plr->economic.luxury,
                     "player%d.rates.luxury", plrno);
  secfile_insert_int(saving->file, plr->economic.infra_points,
                     "player%d.infrapts", plrno);
  secfile_insert_int(saving->file, plr->server.bulbs_last_turn,
                     "player%d.research.bulbs_last_turn", plrno);

  /* Save traits */
  {
    enum trait tr;
    int j;

    for (tr = trait_begin(), j = 0; tr != trait_end(); tr = trait_next(tr), j++) {
      secfile_insert_int(saving->file, plr->ai_common.traits[tr].val,
                         "player%d.trait%d.val", plrno, j);
      secfile_insert_int(saving->file, plr->ai_common.traits[tr].mod,
                         "player%d.trait%d.mod", plrno, j);
    }
  }

  /* Save achievements */
  {
    int j = 0;

    achievements_iterate(pach) {
      if (achievement_player_has(pach, plr)) {
        secfile_insert_str(saving->file, achievement_rule_name(pach),
                           "player%d.achievement%d.name", plrno, j);
        if (pach->first == plr) {
          secfile_insert_bool(saving->file, TRUE,
                              "player%d.achievement%d.first", plrno, j);
        } else {
          secfile_insert_bool(saving->file, FALSE,
                              "player%d.achievement%d.first", plrno, j);
        }

        j++;
      }
    } achievements_iterate_end;

    secfile_insert_int(saving->file, j,
                       "player%d.achievement_count", plrno);
  }

  secfile_insert_int(saving->file, plr->revolution_finishes,
                     "player%d.revolution_finishes", plrno);

  /* Player score */
  secfile_insert_int(saving->file, plr->score.happy,
                     "score%d.happy", plrno);
  secfile_insert_int(saving->file, plr->score.content,
                     "score%d.content", plrno);
  secfile_insert_int(saving->file, plr->score.unhappy,
                     "score%d.unhappy", plrno);
  secfile_insert_int(saving->file, plr->score.angry,
                     "score%d.angry", plrno);
  specialist_type_iterate(sp) {
    secfile_insert_int(saving->file, plr->score.specialists[sp],
                       "score%d.specialists%d", plrno, sp);
  } specialist_type_iterate_end;
  secfile_insert_int(saving->file, plr->score.wonders,
                     "score%d.wonders", plrno);
  secfile_insert_int(saving->file, plr->score.techs,
                     "score%d.techs", plrno);
  secfile_insert_int(saving->file, plr->score.techout,
                     "score%d.techout", plrno);
  secfile_insert_int(saving->file, plr->score.landarea,
                     "score%d.landarea", plrno);
  secfile_insert_int(saving->file, plr->score.settledarea,
                     "score%d.settledarea", plrno);
  secfile_insert_int(saving->file, plr->score.population,
                     "score%d.population", plrno);
  secfile_insert_int(saving->file, plr->score.cities,
                     "score%d.cities", plrno);
  secfile_insert_int(saving->file, plr->score.units,
                     "score%d.units", plrno);
  secfile_insert_int(saving->file, plr->score.pollution,
                     "score%d.pollution", plrno);
  secfile_insert_int(saving->file, plr->score.literacy,
                     "score%d.literacy", plrno);
  secfile_insert_int(saving->file, plr->score.bnp,
                     "score%d.bnp", plrno);
  secfile_insert_int(saving->file, plr->score.mfg,
                     "score%d.mfg", plrno);
  secfile_insert_int(saving->file, plr->score.spaceship,
                     "score%d.spaceship", plrno);
  secfile_insert_int(saving->file, plr->score.units_built,
                     "score%d.units_built", plrno);
  secfile_insert_int(saving->file, plr->score.units_killed,
                     "score%d.units_killed", plrno);
  secfile_insert_int(saving->file, plr->score.units_lost,
                     "score%d.units_lost", plrno);
  secfile_insert_int(saving->file, plr->score.units_used,
                     "score%d.units_used", plrno);
  secfile_insert_int(saving->file, plr->score.culture,
                     "score%d.culture", plrno);
  secfile_insert_int(saving->file, plr->score.game,
                     "score%d.total", plrno);

  /* Save space ship status. */
  secfile_insert_int(saving->file, ship->state, "player%d.spaceship.state",
                     plrno);
  if (ship->state != SSHIP_NONE) {
    char buf[32];
    char st[NUM_SS_STRUCTURALS+1];
    int ssi;

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

    for (ssi = 0; ssi < NUM_SS_STRUCTURALS; ssi++) {
      st[ssi] = BV_ISSET(ship->structure, ssi) ? '1' : '0';
    }
    st[ssi] = '\0';
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

  secfile_insert_int(saving->file, plr->history,
                     "player%d.history", plrno);
  secfile_insert_int(saving->file, plr->server.huts,
                     "player%d.hut_count", plrno);

  secfile_insert_bool(saving->file, plr->server.border_vision,
                      "player%d.border_vision", plrno);

  if (saving->scenario) {
    if (plr->autoselect_weight < 0) { /* Apply default behavior */
      int def = 1; /* We want users to get a player in a scenario */

      if (player_has_flag(plr, PLRF_SCENARIO_RESERVED)) {
        /* This isn't usable player */
        def = 0;
      }

      secfile_insert_int(saving->file, def,
                         "player%d.autoselect_weight", plrno);
    } else {
      secfile_insert_int(saving->file, plr->autoselect_weight,
                         "player%d.autoselect_weight", plrno);
    }
  }
}

/************************************************************************//**
  Load city data
****************************************************************************/
static void sg_load_player_cities(struct loaddata *loading,
                                  struct player *plr)
{
  int ncities, i, plrno = player_number(plr);
  bool tasks_handled;
  int wlist_max_length, routes_max;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  sg_failure_ret(secfile_lookup_int(loading->file, &ncities,
                                    "player%d.ncities", plrno),
                 "%s", secfile_error());

  if (!plr->is_alive && ncities > 0) {
    log_sg("'player%d.ncities' = %d for dead player!", plrno, ncities);
    ncities = 0;
  }

  wlist_max_length = secfile_lookup_int_default(loading->file, 0,
                                                "player%d.wl_max_length",
                                                plrno);
  routes_max = secfile_lookup_int_default(loading->file, 0,
                                          "player%d.routes_max_length", plrno);

  /* Load all cities of the player. */
  for (i = 0; i < ncities; i++) {
    char buf[32];
    struct city *pcity;

    fc_snprintf(buf, sizeof(buf), "player%d.c%d", plrno, i);

    /* Create a dummy city. */
    pcity = create_city_virtual(plr, NULL, buf);
    adv_city_alloc(pcity);
    if (!sg_load_player_city(loading, plr, pcity, buf,
                             wlist_max_length, routes_max)) {
      adv_city_free(pcity);
      destroy_city_virtual(pcity);
      sg_failure_ret(FALSE, "Error loading city %d of player %d.", i, plrno);
    }

    identity_number_reserve(pcity->id);
    idex_register_city(&wld, pcity);

    /* Load the information about the nationality of citizens. This is done
     * here because the city sanity check called by citizens_update() requires
     * that the city is registered. */
    sg_load_player_city_citizens(loading, plr, pcity, buf);

    /* After everything is loaded, but before vision. */
    map_claim_ownership(city_tile(pcity), plr, city_tile(pcity), TRUE);

    /* adding the city contribution to fog-of-war */
    pcity->server.vision = vision_new(plr, city_tile(pcity));
    vision_reveal_tiles(pcity->server.vision, game.server.vision_reveal_tiles);
    city_refresh_vision(pcity);

    city_list_append(plr->cities, pcity);
  }

  tasks_handled = FALSE;
  for (i = 0; !tasks_handled; i++) {
    int city_id;
    struct city *pcity = NULL;

    city_id = secfile_lookup_int_default(loading->file, -1, "player%d.task%d.city",
                                         plrno, i);

    if (city_id != -1) {
      pcity = player_city_by_number(plr, city_id);
    }

    if (pcity != NULL) {
      const char *str;
      int nat_x, nat_y;
      struct worker_task *ptask = fc_malloc(sizeof(struct worker_task));

      nat_x = secfile_lookup_int_default(loading->file, -1, "player%d.task%d.x", plrno, i);
      nat_y = secfile_lookup_int_default(loading->file, -1, "player%d.task%d.y", plrno, i);

      ptask->ptile = native_pos_to_tile(&(wld.map), nat_x, nat_y);

      str = secfile_lookup_str(loading->file, "player%d.task%d.activity", plrno, i);
      ptask->act = unit_activity_by_name(str, fc_strcasecmp);

      sg_failure_ret(unit_activity_is_valid(ptask->act),
                     "Unknown workertask activity %s", str);

      str = secfile_lookup_str(loading->file, "player%d.task%d.target", plrno, i);

      if (strcmp("-", str)) {
        ptask->tgt = extra_type_by_rule_name(str);

        sg_failure_ret(ptask->tgt != NULL,
                       "Unknown workertask target %s", str);
      } else {
        ptask->tgt = NULL;

        if (ptask->act == ACTIVITY_IRRIGATE) {
          ptask->act = ACTIVITY_CULTIVATE;
        } else if (ptask->act == ACTIVITY_MINE) {
          ptask->act = ACTIVITY_MINE;
        }
      }

      ptask->want = secfile_lookup_int_default(loading->file, 1,
                                               "player%d.task%d.want", plrno, i);

      worker_task_list_append(pcity->task_reqs, ptask);
    } else {
      tasks_handled = TRUE;
    }
  }
}

/************************************************************************//**
  Load data for one city. sg_save_player_city() is not defined.
****************************************************************************/
static bool sg_load_player_city(struct loaddata *loading, struct player *plr,
                                struct city *pcity, const char *citystr,
                                int wlist_max_length, int routes_max)
{
  struct player *past;
  const char *kind, *name, *str;
  int id, i, repair, sp_count = 0, workers = 0, value;
  int nat_x, nat_y;
  citizens size;
  const char *stylename;
  int partner;
  int want;
  int tmp_int;
  const struct civ_map *nmap = &(wld.map);
  enum capital_type cap;

  sg_warn_ret_val(secfile_lookup_int(loading->file, &nat_x, "%s.x", citystr),
                  FALSE, "%s", secfile_error());
  sg_warn_ret_val(secfile_lookup_int(loading->file, &nat_y, "%s.y", citystr),
                  FALSE, "%s", secfile_error());
  pcity->tile = native_pos_to_tile(&(wld.map), nat_x, nat_y);
  sg_warn_ret_val(NULL != pcity->tile, FALSE,
                  "%s has invalid center tile (%d, %d)",
                  citystr, nat_x, nat_y);
  sg_warn_ret_val(NULL == tile_city(pcity->tile), FALSE,
                  "%s duplicates city (%d, %d)", citystr, nat_x, nat_y);

  /* Instead of dying, use 'citystr' string for damaged name. */
  city_name_set(pcity, secfile_lookup_str_default(loading->file, citystr,
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
  size = (citizens)value; /* Set the correct type */
  sg_warn_ret_val(value == (int)size, FALSE,
                  "Invalid city size: %d, set to %d", value, size);
  city_size_set(pcity, size);

  cap = capital_type_by_name(secfile_lookup_str_default(loading->file, nullptr,
                                                        "%s.capital", citystr),
                             fc_strcasecmp);

  if (capital_type_is_valid(cap)) {
    pcity->capital = cap;
    if (cap == CAPITAL_PRIMARY) {
      plr->primary_capital_id = pcity->id;
    }
  } else {
    pcity->capital = CAPITAL_NOT;
  }

  for (i = 0; i < loading->specialist.size; i++) {
    sg_warn_ret_val(secfile_lookup_int(loading->file, &value, "%s.nspe%d",
                                       citystr, i),
                    FALSE, "%s", secfile_error());
    pcity->specialists[specialist_index(loading->specialist.order[i])]
      = (citizens)value;
    sp_count += value;
  }

  partner = secfile_lookup_int_default(loading->file, 0, "%s.traderoute0", citystr);
  for (i = 0; partner != 0; i++) {
    struct trade_route *proute = fc_malloc(sizeof(struct trade_route));
    const char *dir;
    const char *good_str;

    /* Append to routes list immediately, so the pointer can be found for freeing
     * even if we abort */
    trade_route_list_append(pcity->routes, proute);

    proute->partner = partner;
    dir = secfile_lookup_str(loading->file, "%s.route_direction%d", citystr, i);
    sg_warn_ret_val(dir != NULL, FALSE,
                    "No traderoute direction found for %s", citystr);
    proute->dir = route_direction_by_name(dir, fc_strcasecmp);
    sg_warn_ret_val(route_direction_is_valid(proute->dir), FALSE,
                    "Illegal route direction %s", dir);
    good_str = secfile_lookup_str(loading->file, "%s.route_good%d", citystr, i);
    sg_warn_ret_val(dir != NULL, FALSE,
                    "No good found for %s", citystr);
    proute->goods = goods_by_rule_name(good_str);
    sg_warn_ret_val(proute->goods != NULL, FALSE,
                    "Illegal good %s", good_str);

    /* Next one */
    partner = secfile_lookup_int_default(loading->file, 0,
                                         "%s.traderoute%d", citystr, i + 1);
  }

  for (; i < routes_max; i++) {
    secfile_entry_ignore(loading->file, "%s.traderoute%d", citystr, i);
    secfile_entry_ignore(loading->file, "%s.route_direction%d", citystr, i);
    secfile_entry_ignore(loading->file, "%s.route_good%d", citystr, i);
  }

  sg_warn_ret_val(secfile_lookup_int(loading->file, &pcity->food_stock,
                                    "%s.food_stock", citystr),
                  FALSE, "%s", secfile_error());
  sg_warn_ret_val(secfile_lookup_int(loading->file, &pcity->shield_stock,
                                    "%s.shield_stock", citystr),
                  FALSE, "%s", secfile_error());
  pcity->history =
    secfile_lookup_int_default(loading->file, 0, "%s.history", citystr);

  pcity->airlift =
    secfile_lookup_int_default(loading->file, 0, "%s.airlift", citystr);
  pcity->was_happy =
    secfile_lookup_bool_default(loading->file, FALSE, "%s.was_happy",
                                citystr);
  pcity->had_famine =
    secfile_lookup_bool_default(loading->file, FALSE, "%s.had_famine",
                                citystr);

  pcity->turn_plague =
    secfile_lookup_int_default(loading->file, 0, "%s.turn_plague", citystr);

  sg_warn_ret_val(secfile_lookup_int(loading->file, &pcity->anarchy,
                                    "%s.anarchy", citystr),
                  FALSE, "%s", secfile_error());
  pcity->rapture =
    secfile_lookup_int_default(loading->file, 0, "%s.rapture", citystr);
  pcity->steal =
    secfile_lookup_int_default(loading->file, 0, "%s.steal", citystr);

  sg_warn_ret_val(secfile_lookup_int(loading->file, &pcity->turn_founded,
                                     "%s.turn_founded", citystr),
                  FALSE, "%s", secfile_error());
  sg_warn_ret_val(secfile_lookup_int(loading->file, &tmp_int,
                                     "%s.acquire_t", citystr),
                  FALSE, "%s", secfile_error());
  pcity->acquire_t = tmp_int;
  sg_warn_ret_val(secfile_lookup_bool(loading->file, &pcity->did_buy, "%s.did_buy",
                                      citystr), FALSE, "%s", secfile_error());
  sg_warn_ret_val(secfile_lookup_bool(loading->file, &pcity->did_sell, "%s.did_sell",
                                      citystr), FALSE, "%s", secfile_error());

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

  want = secfile_lookup_int_default(loading->file, 0,
                                    "%s.current_want", citystr);
  if (pcity->production.kind == VUT_IMPROVEMENT) {
    pcity->server.adv->
      building_want[improvement_index(pcity->production.value.building)]
      = want;
  }

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

  stylename = secfile_lookup_str_default(loading->file, NULL,
                                         "%s.style", citystr);
  if (stylename != NULL) {
    pcity->style = city_style_by_rule_name(stylename);
  } else {
    pcity->style = 0;
  }
  if (pcity->style < 0) {
    pcity->style = city_style(pcity);
  }

  pcity->server.synced = FALSE; /* Must re-sync with clients */

  /* Initialise list of city improvements. */
  for (i = 0; i < ARRAY_SIZE(pcity->built); i++) {
    pcity->built[i].turn = I_NEVER;
  }

  /* Load city improvements. */
  str = secfile_lookup_str(loading->file, "%s.improvements", citystr);
  sg_warn_ret_val(str != NULL, FALSE, "%s", secfile_error());
  sg_warn_ret_val(strlen(str) == loading->improvement.size, FALSE,
                  "Invalid length of '%s.improvements' ("
                  SIZE_T_PRINTF " ~= " SIZE_T_PRINTF ").",
                  citystr, strlen(str), loading->improvement.size);
  for (i = 0; i < loading->improvement.size; i++) {
    sg_warn_ret_val(str[i] == '1' || str[i] == '0', FALSE,
                   "Undefined value '%c' within '%s.improvements'.",
                   str[i], citystr)

    if (str[i] == '1') {
      struct impr_type *pimprove
        = improvement_by_rule_name(loading->improvement.order[i]);

      if (pimprove) {
        city_add_improvement(pcity, pimprove);
      }
    }
  }

  sg_failure_ret_val(loading->worked_tiles != NULL, FALSE,
                     "No worked tiles map defined.");

  city_freeze_workers(pcity);

  /* Load new savegame with variable (squared) city radius and worked
   * tiles map */

  int radius_sq
    = secfile_lookup_int_default(loading->file, -1, "%s.city_radius_sq",
                                 citystr);
  city_map_radius_sq_set(pcity, radius_sq);

  city_tile_iterate(nmap, CITY_MAP_MAX_RADIUS_SQ, city_tile(pcity), ptile) {
    if (loading->worked_tiles[ptile->index] == pcity->id) {
      if (sq_map_distance(ptile, pcity->tile) > radius_sq) {
        log_sg("[%s] '%s' (%d, %d) has worker outside current radius "
               "at (%d, %d); repairing", citystr, city_name_get(pcity),
               TILE_XY(pcity->tile), TILE_XY(ptile));
        pcity->specialists[DEFAULT_SPECIALIST]++;
        sp_count++;
      } else {
        tile_set_worked(ptile, pcity);
        workers++;
      }

#ifdef FREECIV_DEBUG
      /* Set this tile to unused; a check for not reset tiles is
       * included in game_load_internal() */
      loading->worked_tiles[ptile->index] = -1;
#endif /* FREECIV_DEBUG */
    }
  } city_tile_iterate_end;

  if (tile_worked(city_tile(pcity)) != pcity) {
    struct city *pwork = tile_worked(city_tile(pcity));

    if (NULL != pwork) {
      log_sg("[%s] city center of '%s' (%d,%d) [%d] is worked by '%s' "
             "(%d,%d) [%d]; repairing", citystr, city_name_get(pcity),
             TILE_XY(city_tile(pcity)), city_size_get(pcity), city_name_get(pwork),
             TILE_XY(city_tile(pwork)), city_size_get(pwork));

      tile_set_worked(city_tile(pcity), NULL); /* Remove tile from pwork */
      pwork->specialists[DEFAULT_SPECIALIST]++;
      auto_arrange_workers(pwork);
    } else {
      log_sg("[%s] city center of '%s' (%d,%d) [%d] is empty; repairing",
             citystr, city_name_get(pcity), TILE_XY(city_tile(pcity)),
             city_size_get(pcity));
    }

    /* Repair pcity */
    tile_set_worked(city_tile(pcity), pcity);
    city_repair_size(pcity, -1);
  }

  repair = city_size_get(pcity) - sp_count - (workers - FREE_WORKED_TILES);
  if (0 != repair) {
    log_sg("[%s] size mismatch for '%s' (%d,%d): size [%d] != "
           "(workers [%d] - free worked tiles [%d]) + specialists [%d]",
           citystr, city_name_get(pcity), TILE_XY(city_tile(pcity)), city_size_get(pcity),
           workers, FREE_WORKED_TILES, sp_count);

    /* Repair pcity */
    city_repair_size(pcity, repair);
  }

  /* worklist_init() done in create_city_virtual() */
  worklist_load(loading->file, wlist_max_length, &pcity->worklist, "%s", citystr);

  /* Load city options. */
  BV_CLR_ALL(pcity->city_options);
  for (i = 0; i < loading->coptions.size; i++) {
    if (secfile_lookup_bool_default(loading->file, FALSE, "%s.option%d",
                                    citystr, i)) {
      BV_SET(pcity->city_options, loading->coptions.order[i]);
    }
  }
  sg_warn_ret_val(secfile_lookup_int(loading->file, &tmp_int,
                                     "%s.wlcb", citystr),
                  FALSE, "%s", secfile_error());
  pcity->wlcb = tmp_int;

  /* Load the city rally point. */
  {
    int len = secfile_lookup_int_default(loading->file, 0,
                                         "%s.rally_point_length", citystr);
    int unconverted;

    pcity->rally_point.length = len;
    if (len > 0) {
      const char *rally_orders, *rally_dirs, *rally_activities;

      pcity->rally_point.orders
        = fc_malloc(len * sizeof(*(pcity->rally_point.orders)));
      pcity->rally_point.persistent
        = secfile_lookup_bool_default(loading->file, FALSE,
                                      "%s.rally_point_persistent", citystr);
      pcity->rally_point.vigilant
        = secfile_lookup_bool_default(loading->file, FALSE,
                                      "%s.rally_point_vigilant", citystr);

      rally_orders
        = secfile_lookup_str_default(loading->file, "",
                                     "%s.rally_point_orders", citystr);
      rally_dirs
        = secfile_lookup_str_default(loading->file, "",
                                     "%s.rally_point_dirs", citystr);
      rally_activities
        = secfile_lookup_str_default(loading->file, "",
                                     "%s.rally_point_activities", citystr);

      for (i = 0; i < len; i++) {
        struct unit_order *order = &pcity->rally_point.orders[i];

        if (rally_orders[i] == '\0' || rally_dirs[i] == '\0'
            || rally_activities[i] == '\0') {
          log_sg("Invalid rally point.");
          free(pcity->rally_point.orders);
          pcity->rally_point.orders = NULL;
          pcity->rally_point.length = 0;
          break;
        }
        order->order = char2order(rally_orders[i]);
        order->dir = char2dir(rally_dirs[i]);
        order->activity = char2activity(rally_activities[i]);

        unconverted = secfile_lookup_int_default(loading->file, -1,
                                                 "%s.rally_point_action_vec,%d",
                                                 citystr, i);

        if (unconverted == -1) {
          order->action = ACTION_NONE;
        } else if (unconverted >= 0 && unconverted < loading->action.size) {
          /* Look up what action id the unconverted number represents. */
          order->action = loading->action.order[unconverted];
        } else {
          if (order->order == ORDER_PERFORM_ACTION) {
            sg_regr(3020000, "Invalid action id in order for city rally point %d",
                    pcity->id);
          }

          order->action = ACTION_NONE;
        }

        order->target
          = secfile_lookup_int_default(loading->file, NO_TARGET,
                                       "%s.rally_point_tgt_vec,%d",
                                       citystr, i);
        order->sub_target
          = secfile_lookup_int_default(loading->file, -1,
                                       "%s.rally_point_sub_tgt_vec,%d",
                                       citystr, i);
      }
    } else {
      pcity->rally_point.orders = NULL;

      secfile_entry_ignore(loading->file, "%s.rally_point_persistent",
                           citystr);
      secfile_entry_ignore(loading->file, "%s.rally_point_vigilant",
                           citystr);
      secfile_entry_ignore(loading->file, "%s.rally_point_orders",
                           citystr);
      secfile_entry_ignore(loading->file, "%s.rally_point_dirs",
                           citystr);
      secfile_entry_ignore(loading->file, "%s.rally_point_activities",
                           citystr);
      secfile_entry_ignore(loading->file, "%s.rally_point_action_vec",
                           citystr);
      secfile_entry_ignore(loading->file,
                           "%s.rally_point_tgt_vec", citystr);
      secfile_entry_ignore(loading->file,
                           "%s.rally_point_sub_tgt_vec", citystr);
    }
  }

  /* Load the city manager parameters. */
  {
    bool enabled = secfile_lookup_bool_default(loading->file, FALSE,
                                               "%s.cma_enabled", citystr);
    if (enabled) {
      struct cm_parameter *param = fc_calloc(1, sizeof(struct cm_parameter));

      for (i = 0; i < O_LAST; i++) {
        param->minimal_surplus[i] = secfile_lookup_int_default(
            loading->file, 0, "%s.cma_minimal_surplus,%d", citystr, i);
        param->factor[i] = secfile_lookup_int_default(
            loading->file, 0, "%s.cma_factor,%d", citystr, i);
      }

      param->max_growth = secfile_lookup_bool_default(
          loading->file, FALSE, "%s.max_growth", citystr);
      param->require_happy = secfile_lookup_bool_default(
          loading->file, FALSE, "%s.require_happy", citystr);
      param->allow_disorder = secfile_lookup_bool_default(
          loading->file, FALSE, "%s.allow_disorder", citystr);
      param->allow_specialists = secfile_lookup_bool_default(
          loading->file, FALSE, "%s.allow_specialists", citystr);
      param->happy_factor = secfile_lookup_int_default(
          loading->file, 0, "%s.happy_factor", citystr);
      pcity->cm_parameter = param;
    } else {
      pcity->cm_parameter = NULL;

      for (i = 0; i < O_LAST; i++) {
        secfile_entry_ignore(loading->file,
                             "%s.cma_minimal_surplus,%d", citystr, i);
        secfile_entry_ignore(loading->file,
                             "%s.cma_factor,%d", citystr, i);
      }

      secfile_entry_ignore(loading->file, "%s.max_growth",
                           citystr);
      secfile_entry_ignore(loading->file, "%s.require_happy",
                           citystr);
      secfile_entry_ignore(loading->file, "%s.allow_disorder",
                           citystr);
      secfile_entry_ignore(loading->file, "%s.allow_specialists",
                           citystr);
      secfile_entry_ignore(loading->file, "%s.happy_factor",
                           citystr);
    }
  }

  CALL_FUNC_EACH_AI(city_load, loading->file, pcity, citystr);

  return TRUE;
}

/************************************************************************//**
  Load nationality data for one city.
****************************************************************************/
static void sg_load_player_city_citizens(struct loaddata *loading,
                                         struct player *plr,
                                         struct city *pcity,
                                         const char *citystr)
{
  if (game.info.citizen_nationality) {
    citizens size;

    citizens_init(pcity);
    player_slots_iterate(pslot) {
      int nationality;

      nationality = secfile_lookup_int_default(loading->file, -1,
                                               "%s.citizen%d", citystr,
                                               player_slot_index(pslot));
      if (nationality > 0 && !player_slot_is_used(pslot)) {
        log_sg("Citizens of an invalid nation for %s (player slot %d)!",
               city_name_get(pcity), player_slot_index(pslot));
        continue;
      }

      if (nationality != -1 && player_slot_is_used(pslot)) {
        sg_warn(nationality >= 0 && nationality <= MAX_CITY_SIZE,
                "Invalid value for citizens of player %d in %s: %d.",
                player_slot_index(pslot), city_name_get(pcity), nationality);
        citizens_nation_set(pcity, pslot, nationality);
      }
    } player_slots_iterate_end;
    /* Sanity check. */
    size = citizens_count(pcity);
    if (size != city_size_get(pcity)) {
      if (size != 0) {
        /* size == 0 can be result from the fact that ruleset had no
         * nationality enabled at saving time, so no citizens at all
         * were saved. But something more serious must be going on if
         * citizens have been saved partially - if some of them are there. */
        log_sg("City size and number of citizens does not match in %s "
               "(%d != %d)! Repairing ...", city_name_get(pcity),
               city_size_get(pcity), size);
      }
      citizens_update(pcity, NULL);
    }
  }
}

/************************************************************************//**
  Save cities data
****************************************************************************/
static void sg_save_player_cities(struct savedata *saving,
                                  struct player *plr)
{
  int wlist_max_length = 0, rally_point_max_length = 0, routes_max = 0;
  int i = 0;
  int plrno = player_number(plr);
  bool nations[MAX_NUM_PLAYER_SLOTS];

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  secfile_insert_int(saving->file, city_list_size(plr->cities),
                     "player%d.ncities", plrno);

  if (game.info.citizen_nationality) {
    /* Initialise the nation list for the citizens information. */
    player_slots_iterate(pslot) {
      nations[player_slot_index(pslot)] = FALSE;
    } player_slots_iterate_end;
  }

  /* First determine length of longest worklist, rally point order, and the
   * nationalities we have. */
  city_list_iterate(plr->cities, pcity) {
    int routes;

    /* Check the sanity of the city. */
    city_refresh(pcity);
    sanity_check_city(pcity);

    if (pcity->worklist.length > wlist_max_length) {
      wlist_max_length = pcity->worklist.length;
    }

    if (pcity->rally_point.length > rally_point_max_length) {
      rally_point_max_length = pcity->rally_point.length;
    }

    routes = city_num_trade_routes(pcity);
    if (routes > routes_max) {
      routes_max = routes;
    }

    if (game.info.citizen_nationality) {
      /* Find all nations of the citizens,*/
      players_iterate(pplayer) {
        if (!nations[player_index(pplayer)]
            && citizens_nation_get(pcity, pplayer->slot) != 0) {
          nations[player_index(pplayer)] = TRUE;
        }
      } players_iterate_end;
    }
  } city_list_iterate_end;

  secfile_insert_int(saving->file, wlist_max_length,
                     "player%d.wl_max_length", plrno);
  secfile_insert_int(saving->file, routes_max,
                     "player%d.routes_max_length", plrno);

  city_list_iterate(plr->cities, pcity) {
    struct tile *pcenter = city_tile(pcity);
    char impr_buf[B_LAST + 1];
    char buf[32];
    int j, nat_x, nat_y;

    fc_snprintf(buf, sizeof(buf), "player%d.c%d", plrno, i);


    index_to_native_pos(&nat_x, &nat_y, tile_index(pcenter));
    secfile_insert_int(saving->file, nat_y, "%s.y", buf);
    secfile_insert_int(saving->file, nat_x, "%s.x", buf);

    secfile_insert_int(saving->file, pcity->id, "%s.id", buf);

    if (pcity->original != NULL) {
      secfile_insert_int(saving->file, player_number(pcity->original),
                         "%s.original", buf);
    } else {
      secfile_insert_int(saving->file, -1, "%s.original", buf);
    }
    secfile_insert_int(saving->file, city_size_get(pcity), "%s.size", buf);

    secfile_insert_str(saving->file, capital_type_name(pcity->capital),
                       "%s.capital", buf);

    j = 0;
    specialist_type_iterate(sp) {
      secfile_insert_int(saving->file, pcity->specialists[sp], "%s.nspe%d",
                         buf, j++);
    } specialist_type_iterate_end;

    j = 0;
    trade_routes_iterate(pcity, proute) {
      secfile_insert_int(saving->file, proute->partner, "%s.traderoute%d",
                         buf, j);
      secfile_insert_str(saving->file, route_direction_name(proute->dir),
                         "%s.route_direction%d", buf, j);
      secfile_insert_str(saving->file, goods_rule_name(proute->goods),
                           "%s.route_good%d", buf, j);
      j++;
    } trade_routes_iterate_end;

    /* Save dummy values to keep tabular format happy */
    for (; j < routes_max; j++) {
      secfile_insert_int(saving->file, 0, "%s.traderoute%d", buf, j);
      secfile_insert_str(saving->file, route_direction_name(RDIR_BIDIRECTIONAL),
                         "%s.route_direction%d", buf, j);
      secfile_insert_str(saving->file, goods_rule_name(goods_by_number(0)),
                         "%s.route_good%d", buf, j);
    }

    secfile_insert_int(saving->file, pcity->food_stock, "%s.food_stock",
                       buf);
    secfile_insert_int(saving->file, pcity->shield_stock, "%s.shield_stock",
                       buf);
    secfile_insert_int(saving->file, pcity->history, "%s.history",
                       buf);

    secfile_insert_int(saving->file, pcity->airlift, "%s.airlift",
                       buf);
    secfile_insert_bool(saving->file, pcity->was_happy, "%s.was_happy",
                        buf);
    secfile_insert_bool(saving->file, pcity->had_famine, "%s.had_famine",
                        buf);
    secfile_insert_int(saving->file, pcity->turn_plague, "%s.turn_plague",
                       buf);

    secfile_insert_int(saving->file, pcity->anarchy, "%s.anarchy", buf);
    secfile_insert_int(saving->file, pcity->rapture, "%s.rapture", buf);
    secfile_insert_int(saving->file, pcity->steal, "%s.steal", buf);
    secfile_insert_int(saving->file, pcity->turn_founded, "%s.turn_founded",
                       buf);
    secfile_insert_int(saving->file, pcity->acquire_t, "%s.acquire_t", buf);
    secfile_insert_bool(saving->file, pcity->did_buy, "%s.did_buy", buf);
    secfile_insert_bool(saving->file, pcity->did_sell, "%s.did_sell", buf);
    secfile_insert_int(saving->file, pcity->turn_last_built,
                       "%s.turn_last_built", buf);

    /* For visual debugging, variable length strings together here */
    secfile_insert_str(saving->file, city_name_get(pcity), "%s.name", buf);

    secfile_insert_str(saving->file, universal_type_rule_name(&pcity->production),
                       "%s.currently_building_kind", buf);
    secfile_insert_str(saving->file, universal_rule_name(&pcity->production),
                       "%s.currently_building_name", buf);

    if (pcity->production.kind == VUT_IMPROVEMENT) {
      secfile_insert_int(saving->file,
                         pcity->server.adv->
                               building_want[improvement_index(pcity->production.value.building)],
                         "%s.current_want", buf);
    } else {
      secfile_insert_int(saving->file, 0,
                         "%s.current_want", buf);
    }

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

    secfile_insert_str(saving->file, city_style_rule_name(pcity->style),
                       "%s.style", buf);

    /* Save the squared city radius and all tiles within the corresponding
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
                   SIZE_T_PRINTF " < " SIZE_T_PRINTF ").", buf,
                   strlen(impr_buf), sizeof(impr_buf));
    secfile_insert_str(saving->file, impr_buf, "%s.improvements", buf);

    worklist_save(saving->file, &pcity->worklist, wlist_max_length, "%s",
                  buf);

    for (j = 0; j < CITYO_LAST; j++) {
      secfile_insert_bool(saving->file, BV_ISSET(pcity->city_options, j),
                          "%s.option%d", buf, j);
    }
    secfile_insert_int(saving->file, pcity->wlcb,
                       "%s.wlcb", buf);

    CALL_FUNC_EACH_AI(city_save, saving->file, pcity, buf);

    if (game.info.citizen_nationality) {
      /* Save nationality of the citizens,*/
      players_iterate(pplayer) {
        if (nations[player_index(pplayer)]) {
          secfile_insert_int(saving->file,
                             citizens_nation_get(pcity, pplayer->slot),
                             "%s.citizen%d", buf, player_index(pplayer));
        }
      } players_iterate_end;
    }

    secfile_insert_int(saving->file, pcity->rally_point.length,
                       "%s.rally_point_length", buf);
    if (pcity->rally_point.length) {
      int len = pcity->rally_point.length;
      char orders[len + 1], dirs[len + 1], activities[len + 1];
      int actions[len];
      int targets[len];
      int sub_targets[len];

      secfile_insert_bool(saving->file, pcity->rally_point.persistent,
                          "%s.rally_point_persistent", buf);
      secfile_insert_bool(saving->file, pcity->rally_point.vigilant,
                          "%s.rally_point_vigilant", buf);

      for (j = 0; j < len; j++) {
        orders[j] = order2char(pcity->rally_point.orders[j].order);
        dirs[j] = '?';
        activities[j] = '?';
        targets[j] = NO_TARGET;
        sub_targets[j] = NO_TARGET;
        actions[j] = -1;
        switch (pcity->rally_point.orders[j].order) {
        case ORDER_MOVE:
        case ORDER_ACTION_MOVE:
          dirs[j] = dir2char(pcity->rally_point.orders[j].dir);
          break;
        case ORDER_ACTIVITY:
          sub_targets[j] = pcity->rally_point.orders[j].sub_target;
          activities[j]
            = activity2char(pcity->rally_point.orders[j].activity);
          actions[j]
            = pcity->rally_point.orders[j].action;
          break;
        case ORDER_PERFORM_ACTION:
          actions[j] = pcity->rally_point.orders[j].action;
          targets[j] = pcity->rally_point.orders[j].target;
          sub_targets[j] = pcity->rally_point.orders[j].sub_target;
          break;
        case ORDER_FULL_MP:
        case ORDER_LAST:
          break;
        }

        if (actions[j] == ACTION_NONE) {
          actions[j] = -1;
        }
      }
      orders[len] = dirs[len] = activities[len] = '\0';

      secfile_insert_str(saving->file, orders, "%s.rally_point_orders", buf);
      secfile_insert_str(saving->file, dirs, "%s.rally_point_dirs", buf);
      secfile_insert_str(saving->file, activities,
                         "%s.rally_point_activities", buf);

      secfile_insert_int_vec(saving->file, actions, len,
                             "%s.rally_point_action_vec", buf);
      /* Fill in dummy values for order targets so the registry will save
       * the unit table in a tabular format. */
      for (j = len; j < rally_point_max_length; j++) {
        secfile_insert_int(saving->file, -1, "%s.rally_point_action_vec,%d",
                           buf, j);
      }

      secfile_insert_int_vec(saving->file, targets, len,
                             "%s.rally_point_tgt_vec", buf);
      /* Fill in dummy values for order targets so the registry will save
       * the unit table in a tabular format. */
      for (j = len; j < rally_point_max_length; j++) {
        secfile_insert_int(saving->file, NO_TARGET,
                           "%s.rally_point_tgt_vec,%d", buf, j);
      }

      secfile_insert_int_vec(saving->file, sub_targets, len,
                             "%s.rally_point_sub_tgt_vec", buf);
      /* Fill in dummy values for order targets so the registry will save
       * the unit table in a tabular format. */
      for (j = len; j < rally_point_max_length; j++) {
        secfile_insert_int(saving->file, -1, "%s.rally_point_sub_tgt_vec,%d",
                           buf, j);
      }
    } else {
      /* Put all the same fields into the savegame - otherwise the
       * registry code can't correctly use a tabular format and the
       * savegame will be bigger. */
      secfile_insert_bool(saving->file, FALSE, "%s.rally_point_persistent",
                         buf);
      secfile_insert_bool(saving->file, FALSE, "%s.rally_point_vigilant",
                         buf);
      secfile_insert_str(saving->file, "-", "%s.rally_point_orders", buf);
      secfile_insert_str(saving->file, "-", "%s.rally_point_dirs", buf);
      secfile_insert_str(saving->file, "-", "%s.rally_point_activities",
                         buf);

      /* Fill in dummy values for order targets so the registry will save
       * the unit table in a tabular format. */

      /* The start of a vector has no number. */
      secfile_insert_int(saving->file, -1, "%s.rally_point_action_vec",
                         buf);
      for (j = 1; j < rally_point_max_length; j++) {
        secfile_insert_int(saving->file, -1, "%s.rally_point_action_vec,%d",
                           buf, j);
      }

      /* The start of a vector has no number. */
      secfile_insert_int(saving->file, NO_TARGET, "%s.rally_point_tgt_vec",
                         buf);
      for (j = 1; j < rally_point_max_length; j++) {
        secfile_insert_int(saving->file, NO_TARGET,
                           "%s.rally_point_tgt_vec,%d", buf, j);
      }

      /* The start of a vector has no number. */
      secfile_insert_int(saving->file, -1, "%s.rally_point_sub_tgt_vec",
                         buf);
      for (j = 1; j < rally_point_max_length; j++) {
        secfile_insert_int(saving->file, -1, "%s.rally_point_sub_tgt_vec,%d",
                           buf, j);
      }
    }

    secfile_insert_bool(saving->file, pcity->cm_parameter != NULL,
                       "%s.cma_enabled", buf);
    if (pcity->cm_parameter) {
      secfile_insert_int_vec(saving->file,
                             pcity->cm_parameter->minimal_surplus, O_LAST,
                             "%s.cma_minimal_surplus", buf);
      secfile_insert_int_vec(saving->file,
                             pcity->cm_parameter->factor, O_LAST,
                             "%s.cma_factor", buf);
      secfile_insert_bool(saving->file, pcity->cm_parameter->max_growth,
                          "%s.max_growth", buf);
      secfile_insert_bool(saving->file, pcity->cm_parameter->require_happy,
                          "%s.require_happy", buf);
      secfile_insert_bool(saving->file, pcity->cm_parameter->allow_disorder,
                          "%s.allow_disorder", buf);
      secfile_insert_bool(saving->file,
                          pcity->cm_parameter->allow_specialists,
                         "%s.allow_specialists", buf);
      secfile_insert_int(saving->file, pcity->cm_parameter->happy_factor,
                        "%s.happy_factor", buf);
    } else {
      int zeros[O_LAST];

      memset(zeros, 0, sizeof(zeros));
      secfile_insert_int_vec(saving->file, zeros, O_LAST,
                             "%s.cma_minimal_surplus", buf);
      secfile_insert_int_vec(saving->file, zeros, O_LAST,
                             "%s.cma_factor", buf);
      secfile_insert_bool(saving->file, FALSE, "%s.max_growth", buf);
      secfile_insert_bool(saving->file, FALSE, "%s.require_happy", buf);
      secfile_insert_bool(saving->file, FALSE, "%s.allow_disorder", buf);
      secfile_insert_bool(saving->file, FALSE, "%s.allow_specialists", buf);
      secfile_insert_int(saving->file, 0, "%s.happy_factor", buf);
    }

    i++;
  } city_list_iterate_end;

  i = 0;
  city_list_iterate(plr->cities, pcity) {
    worker_task_list_iterate(pcity->task_reqs, ptask) {
      int nat_x, nat_y;

      index_to_native_pos(&nat_x, &nat_y, tile_index(ptask->ptile));
      secfile_insert_int(saving->file, pcity->id, "player%d.task%d.city",
                         plrno, i);
      secfile_insert_int(saving->file, nat_y, "player%d.task%d.y", plrno, i);
      secfile_insert_int(saving->file, nat_x, "player%d.task%d.x", plrno, i);
      secfile_insert_str(saving->file, unit_activity_name(ptask->act),
                         "player%d.task%d.activity",
                         plrno, i);
      if (ptask->tgt != NULL) {
        secfile_insert_str(saving->file, extra_rule_name(ptask->tgt),
                           "player%d.task%d.target",
                           plrno, i);
      } else {
        secfile_insert_str(saving->file, "-",
                           "player%d.task%d.target",
                           plrno, i);
      }
      secfile_insert_int(saving->file, ptask->want, "player%d.task%d.want", plrno, i);

      i++;
    } worker_task_list_iterate_end;
  } city_list_iterate_end;
}

/************************************************************************//**
  Load unit data
****************************************************************************/
static void sg_load_player_units(struct loaddata *loading,
                                 struct player *plr)
{
  int nunits, i, plrno = player_number(plr);
  size_t orders_max_length;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  sg_failure_ret(secfile_lookup_int(loading->file, &nunits,
                                    "player%d.nunits", plrno),
                 "%s", secfile_error());
  if (!plr->is_alive && nunits > 0) {
    log_sg("'player%d.nunits' = %d for dead player!", plrno, nunits);
    nunits = 0; /* Some old savegames may be buggy. */
  }

  orders_max_length = secfile_lookup_int_default(loading->file, 0,
                                                 "player%d.orders_max_length",
                                                 plrno);

  for (i = 0; i < nunits; i++) {
    struct unit *punit;
    struct city *pcity;
    const char *name;
    char buf[32];
    struct unit_type *type;
    struct tile *ptile;

    fc_snprintf(buf, sizeof(buf), "player%d.u%d", plrno, i);

    name = secfile_lookup_str(loading->file, "%s.type_by_name", buf);
    type = unit_type_by_rule_name(name);
    sg_failure_ret(type != NULL, "%s: unknown unit type \"%s\".", buf, name);

    /* Create a dummy unit. */
    punit = unit_virtual_create(plr, NULL, type, 0);
    if (!sg_load_player_unit(loading, plr, punit, orders_max_length, buf)) {
      unit_virtual_destroy(punit);
      sg_failure_ret(FALSE, "Error loading unit %d of player %d.", i, plrno);
    }

    identity_number_reserve(punit->id);
    idex_register_unit(&wld, punit);

    if ((pcity = game_city_by_number(punit->homecity))) {
      unit_list_prepend(pcity->units_supported, punit);
    } else if (punit->homecity > IDENTITY_NUMBER_ZERO) {
      log_sg("%s: bad home city %d.", buf, punit->homecity);
      punit->homecity = IDENTITY_NUMBER_ZERO;
    }

    ptile = unit_tile(punit);

    /* allocate the unit's contribution to fog of war */
    punit->server.vision = vision_new(unit_owner(punit), ptile);
    unit_refresh_vision(punit);
    /* NOTE: There used to be some map_set_known calls here.  These were
     * unneeded since unfogging the tile when the unit sees it will
     * automatically reveal that tile. */

    unit_list_append(plr->units, punit);
    unit_list_prepend(unit_tile(punit)->units, punit);
  }
}

/************************************************************************//**
  Load one unit. sg_save_player_unit() is not defined.
****************************************************************************/
static bool sg_load_player_unit(struct loaddata *loading,
                                struct player *plr, struct unit *punit,
                                int orders_max_length,
                                const char *unitstr)
{
  enum unit_activity activity;
  int nat_x, nat_y;
  struct extra_type *pextra = NULL;
  struct tile *ptile;
  int extra_id;
  int ei;
  const char *facing_str;
  int natnbr;
  int unconverted;
  const char *str;
  enum gen_action action;

  sg_warn_ret_val(secfile_lookup_int(loading->file, &punit->id, "%s.id",
                                     unitstr), FALSE, "%s", secfile_error());
  sg_warn_ret_val(secfile_lookup_int(loading->file, &nat_x, "%s.x", unitstr),
                  FALSE, "%s", secfile_error());
  sg_warn_ret_val(secfile_lookup_int(loading->file, &nat_y, "%s.y", unitstr),
                  FALSE, "%s", secfile_error());

  ptile = native_pos_to_tile(&(wld.map), nat_x, nat_y);
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

  /* If savegame has unit nationality, it doesn't hurt to
   * internally set it even if nationality rules are disabled. */
  natnbr = secfile_lookup_int_default(loading->file,
                                      player_number(plr),
                                      "%s.nationality", unitstr);

  punit->nationality = player_by_number(natnbr);
  if (punit->nationality == NULL) {
    punit->nationality = plr;
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
  activity = unit_activity_by_name(loading->activities.order[ei],
                                   fc_strcasecmp);
  sg_warn_ret_val(secfile_lookup_int(loading->file, &ei,
                                     "%s.action", unitstr), FALSE,
                  "%s", secfile_error());
  if (ei == -1) {
    action = ACTION_NONE;
  } else if (ei >= 0 && ei < loading->action.size) {
    action = loading->action.order[ei];
  } else {
    log_sg("Invalid action id for unit %d", punit->id);
    action = ACTION_NONE;
  }

  punit->birth_turn
    = secfile_lookup_int_default(loading->file, game.info.turn,
                                 "%s.born", unitstr);
  punit->current_form_turn
    = secfile_lookup_int_default(loading->file, game.info.turn,
                                 "%s.current_form_turn", unitstr);

  extra_id = secfile_lookup_int_default(loading->file, -2,
                                        "%s.activity_tgt", unitstr);

  if (extra_id != -2) {
    if (extra_id >= 0 && extra_id < loading->extra.size) {
      pextra = loading->extra.order[extra_id];
      set_unit_activity_targeted(punit, activity, pextra, action);
    } else if (activity == ACTIVITY_IRRIGATE) {
      struct extra_type *tgt = next_extra_for_tile(unit_tile(punit),
                                                   EC_IRRIGATION,
                                                   unit_owner(punit),
                                                   punit);
      if (tgt != NULL) {
        set_unit_activity_targeted(punit, ACTIVITY_IRRIGATE, tgt, action);
      } else {
        set_unit_activity(punit, ACTIVITY_CULTIVATE, action);
      }
    } else if (activity == ACTIVITY_MINE) {
      struct extra_type *tgt = next_extra_for_tile(unit_tile(punit),
                                                   EC_MINE,
                                                   unit_owner(punit),
                                                   punit);
      if (tgt != NULL) {
        set_unit_activity_targeted(punit, ACTIVITY_MINE, tgt, action);
      } else {
        set_unit_activity(punit, ACTIVITY_PLANT, action);
      }
    } else {
      set_unit_activity(punit, activity, action);
    }
  } else {
    set_unit_activity_targeted(punit, activity, NULL, action);
  } /* activity_tgt == NULL */

  sg_warn_ret_val(secfile_lookup_int(loading->file, &punit->activity_count,
                                     "%s.activity_count", unitstr), FALSE,
                  "%s", secfile_error());

  punit->changed_from =
    secfile_lookup_int_default(loading->file, ACTIVITY_IDLE,
                               "%s.changed_from", unitstr);

  sg_warn_ret_val(secfile_lookup_int(loading->file, &extra_id,
                                     "%s.changed_from_tgt", unitstr), FALSE,
                  "%s", secfile_error());

  if (extra_id >= 0 && extra_id < loading->extra.size) {
    punit->changed_from_target = loading->extra.order[extra_id];
  } else {
    punit->changed_from_target = NULL;
  }

  punit->changed_from_count =
    secfile_lookup_int_default(loading->file, 0,
                               "%s.changed_from_count", unitstr);

  /* Special case: for a long time, we accidentally incremented
   * activity_count while a unit was sentried, so it could increase
   * without bound (bug #20641) and be saved in old savefiles.
   * We zero it to prevent potential trouble overflowing the range
   * in network packets, etc. */
  if (activity == ACTIVITY_SENTRY) {
    punit->activity_count = 0;
  }
  if (punit->changed_from == ACTIVITY_SENTRY) {
    punit->changed_from_count = 0;
  }

  punit->veteran
    = secfile_lookup_int_default(loading->file, 0, "%s.veteran", unitstr);
  {
    /* Protect against change in veteran system in ruleset */
    const int levels = utype_veteran_levels(unit_type_get(punit));

    if (punit->veteran >= levels) {
      fc_assert(levels >= 1);
      punit->veteran = levels - 1;
    }
  }
  punit->done_moving
    = secfile_lookup_bool_default(loading->file, (punit->moves_left == 0),
                                  "%s.done_moving", unitstr);
  punit->battlegroup
    = secfile_lookup_int_default(loading->file, BATTLEGROUP_NONE,
                                 "%s.battlegroup", unitstr);

  if (secfile_lookup_bool_default(loading->file, FALSE,
                                  "%s.go", unitstr)) {
    int gnat_x, gnat_y;

    sg_warn_ret_val(secfile_lookup_int(loading->file, &gnat_x,
                                       "%s.goto_x", unitstr), FALSE,
                    "%s", secfile_error());
    sg_warn_ret_val(secfile_lookup_int(loading->file, &gnat_y,
                                       "%s.goto_y", unitstr), FALSE,
                    "%s", secfile_error());

    punit->goto_tile = native_pos_to_tile(&(wld.map), gnat_x, gnat_y);
  } else {
    punit->goto_tile = NULL;

    if (punit->activity == ACTIVITY_GOTO) {
      /* goto_tile should never be NULL with ACTIVITY_GOTO */
      sg_regr(3020200, "Unit %d on goto without goto_tile. Aborting goto.",
              punit->id);
      punit->activity = ACTIVITY_IDLE;
    }

    /* These variables are not used but needed for saving the unit table.
     * Load them to prevent unused variables errors. */
    secfile_entry_ignore(loading->file, "%s.goto_x", unitstr);
    secfile_entry_ignore(loading->file, "%s.goto_y", unitstr);
  }

  /* Load AI data of the unit. */
  CALL_FUNC_EACH_AI(unit_load, loading->file, punit, unitstr);

  unconverted
   = secfile_lookup_int_default(loading->file, 0,
                                "%s.server_side_agent",
                                unitstr);
  if (unconverted >= 0 && unconverted < loading->ssa.size) {
    /* Look up what server side agent the unconverted number represents. */
    punit->ssa_controller = loading->ssa.order[unconverted];
  } else {
    log_sg("Invalid server side agent %d for unit %d",
           unconverted, punit->id);

    punit->ssa_controller = SSA_NONE;
  }

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
  str = secfile_lookup_str_default(loading->file, "", "%s.carrying", unitstr);
  if (str[0] != '\0') {
    punit->carrying = goods_by_rule_name(str);
  }

  /* The transport status (punit->transported_by) is loaded in
   * sg_player_units_transport(). */

  /* Initialize upkeep values: these are hopefully initialized
   * elsewhere before use (specifically, in city_support(); but
   * fixme: check whether always correctly initialized?).
   * Below is mainly for units which don't have homecity --
   * otherwise these don't get initialized (and AI calculations
   * etc may use junk values). */
  output_type_iterate(o) {
    punit->upkeep[o] = utype_upkeep_cost(unit_type_get(punit), plr, o);
  } output_type_iterate_end;

  sg_warn_ret_val(secfile_lookup_int(loading->file, &unconverted,
                                     "%s.action_decision", unitstr),
                  FALSE, "%s", secfile_error());

  if (unconverted >= 0 && unconverted < loading->act_dec.size) {
    /* Look up what action decision want the unconverted number
     * represents. */
    punit->action_decision_want = loading->act_dec.order[unconverted];
  } else {
    log_sg("Invalid action decision want for unit %d", punit->id);

    punit->action_decision_want = ACT_DEC_NOTHING;
  }

  if (punit->action_decision_want != ACT_DEC_NOTHING) {
    /* Load the tile to act against. */
    int adwt_x, adwt_y;

    if (secfile_lookup_int(loading->file, &adwt_x,
                           "%s.action_decision_tile_x", unitstr)
        && secfile_lookup_int(loading->file, &adwt_y,
                              "%s.action_decision_tile_y", unitstr)) {
      punit->action_decision_tile = native_pos_to_tile(&(wld.map),
                                                       adwt_x, adwt_y);
    } else {
      punit->action_decision_want = ACT_DEC_NOTHING;
      punit->action_decision_tile = NULL;
      log_sg("Bad action_decision_tile for unit %d", punit->id);
    }
  } else {
    secfile_entry_ignore(loading->file, "%s.action_decision_tile_x", unitstr);
    secfile_entry_ignore(loading->file, "%s.action_decision_tile_y", unitstr);

    punit->action_decision_tile = NULL;
  }

  punit->stay = secfile_lookup_bool_default(loading->file, FALSE, "%s.stay", unitstr);

  /* Load the unit orders */
  {
    int len = secfile_lookup_int_default(loading->file, 0,
                                         "%s.orders_length", unitstr);

    if (len > 0) {
      const char *orders_unitstr, *dir_unitstr, *act_unitstr;
      int j;

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

      punit->has_orders = TRUE;

      for (j = 0; j < len; j++) {
        struct unit_order *order = &punit->orders.list[j];
        bool action_wants_extra = FALSE;
        int order_sub_tgt;

        if (orders_unitstr[j] == '\0' || dir_unitstr[j] == '\0'
            || act_unitstr[j] == '\0') {
          log_sg("Invalid unit orders.");
          free_unit_orders(punit);
          break;
        }
        order->order = char2order(orders_unitstr[j]);
        order->dir = char2dir(dir_unitstr[j]);
        order->activity = char2activity(act_unitstr[j]);

        unconverted = secfile_lookup_int_default(loading->file, ACTION_NONE,
                                                 "%s.action_vec,%d",
                                                 unitstr, j);

        if (unconverted == -1) {
          order->action = ACTION_NONE;
        } else if (unconverted >= 0 && unconverted < loading->action.size) {
          /* Look up what action id the unconverted number represents. */
          order->action = loading->action.order[unconverted];
        } else {
          if (order->order == ORDER_PERFORM_ACTION) {
            sg_regr(3020000, "Invalid action id in order for unit %d", punit->id);
          }

          order->action = ACTION_NONE;
        }

        if (order->order == ORDER_LAST
            || (order->order == ORDER_MOVE && !direction8_is_valid(order->dir))
            || (order->order == ORDER_ACTION_MOVE
                && !direction8_is_valid(order->dir))
            || (order->order == ORDER_PERFORM_ACTION
                && !action_id_exists(order->action))
            || (order->order == ORDER_ACTIVITY
                && (order->activity == ACTIVITY_LAST
                    || !action_id_exists(order->action)))) {
          /* An invalid order. Just drop the orders for this unit. */
          free(punit->orders.list);
          punit->orders.list = NULL;
          punit->orders.length = 0;
          punit->has_orders = FALSE;
          punit->goto_tile = NULL;
          break;
        }

        order->target = secfile_lookup_int_default(loading->file, NO_TARGET,
                                                   "%s.tgt_vec,%d",
                                                   unitstr, j);
        order_sub_tgt = secfile_lookup_int_default(loading->file, -1,
                                                   "%s.sub_tgt_vec,%d",
                                                   unitstr, j);

        if (order->order == ORDER_PERFORM_ACTION) {
          /* Validate sub target */
          switch (action_id_get_sub_target_kind(order->action)) {
          case ASTK_BUILDING:
            /* Sub target is a building. */
            if (order_sub_tgt < 0
                || order_sub_tgt >= loading->improvement.size
                || !loading->improvement.order[order_sub_tgt]) {
              /* Sub target is invalid. */
              log_sg("Cannot find building %d for %s to %s",
                     order_sub_tgt, unit_rule_name(punit),
                     action_id_name_translation(order->action));
              order->sub_target = B_LAST;
            } else {
              struct impr_type *pimprove
                = improvement_by_rule_name(loading->improvement.order[order_sub_tgt]);

              if (pimprove != NULL) {
                order->sub_target = improvement_index(pimprove);
              }
            }
            break;
          case ASTK_TECH:
            /* Sub target is a technology. */
            {
              bool failure = order_sub_tgt < 0
                || order_sub_tgt >= loading->technology.size
                || !loading->technology.order[order_sub_tgt];
              struct advance *padvance = NULL;

              if (!failure) {
                padvance = advance_by_rule_name(loading->technology.order[order_sub_tgt]);
              }
              if (padvance == NULL) {
                /* Target tech is invalid. */
                log_sg("Cannot find tech %d for %s to steal",
                       order_sub_tgt, unit_rule_name(punit));
                order->sub_target = A_NONE;
              } else {
                order->sub_target = advance_index(padvance);
              }
            }
            break;
          case ASTK_EXTRA:
          case ASTK_EXTRA_NOT_THERE:
            /* These take an extra. */
            action_wants_extra = TRUE;
            break;
          case ASTK_NONE:
            /* None of these can take a sub target. */
            fc_assert_msg(order_sub_tgt == -1,
                          "Specified sub target for action %d unsupported.",
                          order->action);
            order->sub_target = NO_TARGET;
            break;
          case ASTK_COUNT:
            fc_assert_msg(order_sub_tgt == -1,
                          "Bad action action %d.",
                          order->action);
            order->sub_target = NO_TARGET;
            break;
          }
        }

        if (order->order == ORDER_ACTIVITY || action_wants_extra) {
          enum unit_activity act;

          if (order_sub_tgt < 0 || order_sub_tgt >= loading->extra.size
              || !loading->extra.order[order_sub_tgt]) {
            if (order_sub_tgt != EXTRA_NONE) {
              log_sg("Cannot find extra %d for %s to build",
                     order_sub_tgt, unit_rule_name(punit));
            }

            order->sub_target = EXTRA_NONE;
          } else {
            order->sub_target = extra_index(loading->extra.order[order_sub_tgt]);
          }

          /* An action or an activity may require an extra target. */
          if (action_wants_extra) {
            act = action_id_get_activity(order->action);
          } else {
            act = order->activity;
          }

          if (unit_activity_is_valid(act)
              && unit_activity_needs_target_from_client(act)
              && order->sub_target == EXTRA_NONE) {
            /* Missing required action extra target. */
            free(punit->orders.list);
            punit->orders.list = NULL;
            punit->orders.length = 0;
            punit->has_orders = FALSE;
            punit->goto_tile = NULL;
            break;
          }
        } else if (order->order != ORDER_PERFORM_ACTION) {
          if (order_sub_tgt != -1) {
            log_sg("Unexpected sub_target %d (expected %d) for order type %d",
                   order_sub_tgt, -1, order->order);
          }
          order->sub_target = NO_TARGET;
        }
      }

      for (; j < orders_max_length; j++) {
        secfile_entry_ignore(loading->file,
                             "%s.action_vec,%d", unitstr, j);
        secfile_entry_ignore(loading->file,
                             "%s.tgt_vec,%d", unitstr, j);
        secfile_entry_ignore(loading->file,
                             "%s.sub_tgt_vec,%d", unitstr, j);
      }
    } else {
      int j;

      punit->has_orders = FALSE;

      /* Never nullify goto_tile for a unit that is in active goto. */
      if (punit->activity != ACTIVITY_GOTO) {
        punit->goto_tile = NULL;
      }

      punit->orders.list = NULL;
      punit->orders.length = 0;

      secfile_entry_ignore(loading->file, "%s.orders_index", unitstr);
      secfile_entry_ignore(loading->file, "%s.orders_repeat", unitstr);
      secfile_entry_ignore(loading->file, "%s.orders_vigilant", unitstr);
      secfile_entry_ignore(loading->file, "%s.orders_list", unitstr);
      secfile_entry_ignore(loading->file, "%s.dir_list", unitstr);
      secfile_entry_ignore(loading->file, "%s.activity_list", unitstr);
      secfile_entry_ignore(loading->file, "%s.action_vec", unitstr);
      secfile_entry_ignore(loading->file, "%s.tgt_vec", unitstr);
      secfile_entry_ignore(loading->file, "%s.sub_tgt_vec", unitstr);

      for (j = 1; j < orders_max_length; j++) {
        secfile_entry_ignore(loading->file,
                             "%s.action_vec,%d", unitstr, j);
        secfile_entry_ignore(loading->file,
                             "%s.tgt_vec,%d", unitstr, j);
        secfile_entry_ignore(loading->file,
                             "%s.sub_tgt_vec,%d", unitstr, j);
      }
    }
  }

  return TRUE;
}

/************************************************************************//**
  Load the transport status of all units. This is separated from the other
  code as all units must be known.
****************************************************************************/
static void sg_load_player_units_transport(struct loaddata *loading,
                                           struct player *plr)
{
  int nunits, i, plrno = player_number(plr);

  /* Check status and return if not OK (sg_success FALSE). */
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
#ifndef FREECIV_NDEBUG
      bool load_success =
#endif
        unit_transport_load(punit, ptrans, TRUE);

      fc_assert_action(load_success, continue);
    }
  }
}

/************************************************************************//**
  Save unit data
****************************************************************************/
static void sg_save_player_units(struct savedata *saving,
                                 struct player *plr)
{
  int i = 0;
  int longest_order = 0;
  int plrno = player_number(plr);

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  secfile_insert_int(saving->file, unit_list_size(plr->units),
                     "player%d.nunits", plrno);

  /* Find the longest unit order so different order length won't break
   * storing units in the tabular format. */
  unit_list_iterate(plr->units, punit) {
    if (punit->has_orders) {
      if (longest_order < punit->orders.length) {
        longest_order = punit->orders.length;
      }
    }
  } unit_list_iterate_end;

  secfile_insert_int(saving->file, longest_order,
                     "player%d.orders_max_length", plrno);

  unit_list_iterate(plr->units, punit) {
    char buf[32];
    char dirbuf[2] = " ";
    int nat_x, nat_y;
    int last_order, j;

    fc_snprintf(buf, sizeof(buf), "player%d.u%d", plrno, i);
    dirbuf[0] = dir2char(punit->facing);
    secfile_insert_int(saving->file, punit->id, "%s.id", buf);

    index_to_native_pos(&nat_x, &nat_y, tile_index(unit_tile(punit)));
    secfile_insert_int(saving->file, nat_x, "%s.x", buf);
    secfile_insert_int(saving->file, nat_y, "%s.y", buf);

    secfile_insert_str(saving->file, dirbuf, "%s.facing", buf);
    if (game.info.citizen_nationality) {
      secfile_insert_int(saving->file, player_number(unit_nationality(punit)),
                         "%s.nationality", buf);
    }
    secfile_insert_int(saving->file, punit->veteran, "%s.veteran", buf);
    secfile_insert_int(saving->file, punit->hp, "%s.hp", buf);
    secfile_insert_int(saving->file, punit->homecity, "%s.homecity", buf);
    secfile_insert_str(saving->file, unit_rule_name(punit),
                       "%s.type_by_name", buf);

    secfile_insert_int(saving->file, punit->activity, "%s.activity", buf);
    secfile_insert_int(saving->file, punit->activity_count,
                       "%s.activity_count", buf);
    if (punit->action == ACTION_NONE) {
      secfile_insert_int(saving->file, -1, "%s.action", buf);
    } else {
      secfile_insert_int(saving->file, punit->action, "%s.action", buf);
    }
    if (punit->activity_target == NULL) {
      secfile_insert_int(saving->file, -1, "%s.activity_tgt", buf);
    } else {
      secfile_insert_int(saving->file, extra_index(punit->activity_target),
                         "%s.activity_tgt", buf);
    }

    secfile_insert_int(saving->file, punit->changed_from,
                       "%s.changed_from", buf);
    secfile_insert_int(saving->file, punit->changed_from_count,
                       "%s.changed_from_count", buf);
    if (punit->changed_from_target == NULL) {
      secfile_insert_int(saving->file, -1, "%s.changed_from_tgt", buf);
    } else {
      secfile_insert_int(saving->file, extra_index(punit->changed_from_target),
                         "%s.changed_from_tgt", buf);
    }

    secfile_insert_bool(saving->file, punit->done_moving,
                        "%s.done_moving", buf);
    secfile_insert_int(saving->file, punit->moves_left, "%s.moves", buf);
    secfile_insert_int(saving->file, punit->fuel, "%s.fuel", buf);
    secfile_insert_int(saving->file, punit->birth_turn,
                      "%s.born", buf);
    secfile_insert_int(saving->file, punit->current_form_turn,
                      "%s.current_form_turn", buf);
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

    secfile_insert_int(saving->file, punit->ssa_controller,
                        "%s.server_side_agent", buf);

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
    if (punit->carrying != NULL) {
      secfile_insert_str(saving->file, goods_rule_name(punit->carrying),
                         "%s.carrying", buf);
    } else {
      secfile_insert_str(saving->file, "", "%s.carrying", buf);
    }

    secfile_insert_int(saving->file, punit->action_decision_want,
                       "%s.action_decision", buf);

    /* Stored as tile rather than direction to make sure the target tile is
     * sane. */
    if (punit->action_decision_tile) {
      index_to_native_pos(&nat_x, &nat_y,
                          tile_index(punit->action_decision_tile));
      secfile_insert_int(saving->file, nat_x,
                         "%s.action_decision_tile_x", buf);
      secfile_insert_int(saving->file, nat_y,
                         "%s.action_decision_tile_y", buf);
    } else {
      /* Dummy values to get tabular format. */
      secfile_insert_int(saving->file, -1,
                         "%s.action_decision_tile_x", buf);
      secfile_insert_int(saving->file, -1,
                         "%s.action_decision_tile_y", buf);
    }

    secfile_insert_bool(saving->file, punit->stay,
                        "%s.stay", buf);

    if (punit->has_orders) {
      int len = punit->orders.length;
      char orders_buf[len + 1], dir_buf[len + 1];
      char act_buf[len + 1];
      int action_buf[len];
      int tgt_vec[len];
      int sub_tgt_vec[len];

      last_order = len;

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
        tgt_vec[j] = NO_TARGET;
        sub_tgt_vec[j] = -1;
        action_buf[j] = -1;
        switch (punit->orders.list[j].order) {
        case ORDER_MOVE:
        case ORDER_ACTION_MOVE:
          dir_buf[j] = dir2char(punit->orders.list[j].dir);
          break;
        case ORDER_ACTIVITY:
          sub_tgt_vec[j] = punit->orders.list[j].sub_target;
          act_buf[j] = activity2char(punit->orders.list[j].activity);
          action_buf[j] = punit->orders.list[j].action;
          break;
        case ORDER_PERFORM_ACTION:
          action_buf[j] = punit->orders.list[j].action;
          tgt_vec[j] = punit->orders.list[j].target;
          sub_tgt_vec[j] = punit->orders.list[j].sub_target;
          break;
        case ORDER_FULL_MP:
        case ORDER_LAST:
          break;
        }

        if (action_buf[j] == ACTION_NONE) {
          action_buf[j] = -1;
        }
      }
      orders_buf[len] = dir_buf[len] = act_buf[len] = '\0';

      secfile_insert_str(saving->file, orders_buf, "%s.orders_list", buf);
      secfile_insert_str(saving->file, dir_buf, "%s.dir_list", buf);
      secfile_insert_str(saving->file, act_buf, "%s.activity_list", buf);

      secfile_insert_int_vec(saving->file, action_buf, len,
                             "%s.action_vec", buf);
      /* Fill in dummy values for order targets so the registry will save
       * the unit table in a tabular format. */
      for (j = last_order; j < longest_order; j++) {
        secfile_insert_int(saving->file, -1, "%s.action_vec,%d", buf, j);
      }

      secfile_insert_int_vec(saving->file, tgt_vec, len,
                             "%s.tgt_vec", buf);
      /* Fill in dummy values for order targets so the registry will save
       * the unit table in a tabular format. */
      for (j = last_order; j < longest_order; j++) {
        secfile_insert_int(saving->file, NO_TARGET, "%s.tgt_vec,%d", buf, j);
      }

      secfile_insert_int_vec(saving->file, sub_tgt_vec, len,
                             "%s.sub_tgt_vec", buf);
      /* Fill in dummy values for order targets so the registry will save
       * the unit table in a tabular format. */
      for (j = last_order; j < longest_order; j++) {
        secfile_insert_int(saving->file, -1, "%s.sub_tgt_vec,%d", buf, j);
      }
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

      /* Fill in dummy values for order targets so the registry will save
       * the unit table in a tabular format. */

      /* The start of a vector has no number. */
      secfile_insert_int(saving->file, -1, "%s.action_vec", buf);
      for (j = 1; j < longest_order; j++) {
        secfile_insert_int(saving->file, -1, "%s.action_vec,%d", buf, j);
      }

      /* The start of a vector has no number. */
      secfile_insert_int(saving->file, NO_TARGET, "%s.tgt_vec", buf);
      for (j = 1; j < longest_order; j++) {
        secfile_insert_int(saving->file, NO_TARGET, "%s.tgt_vec,%d", buf, j);
      }

      /* The start of a vector has no number. */
      secfile_insert_int(saving->file, -1, "%s.sub_tgt_vec", buf);
      for (j = 1; j < longest_order; j++) {
        secfile_insert_int(saving->file, -1, "%s.sub_tgt_vec,%d", buf, j);
      }
    }

    i++;
  } unit_list_iterate_end;
}

/************************************************************************//**
  Load player (client) attributes data
****************************************************************************/
static void sg_load_player_attributes(struct loaddata *loading,
                                      struct player *plr)
{
  int plrno = player_number(plr);

  /* Check status and return if not OK (sg_success FALSE). */
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
    int quoted_length;
    char *quoted;
#ifndef FREECIV_NDEBUG
    size_t actual_length;
#endif

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
      log_debug("attribute_v2_block_length_quoted=%d"
                " have=" SIZE_T_PRINTF " part=" SIZE_T_PRINTF,
                quoted_length, strlen(quoted), strlen(current));
      fc_assert(strlen(quoted) + strlen(current) <= quoted_length);
      strcat(quoted, current);
    }
    fc_assert_msg(quoted_length == strlen(quoted),
                  "attribute_v2_block_length_quoted=%d"
                  " actual=" SIZE_T_PRINTF,
                  quoted_length, strlen(quoted));

#ifndef FREECIV_NDEBUG
    actual_length =
#endif
        unquote_block(quoted,
                      plr->attribute_block.data,
                      plr->attribute_block.length);
    fc_assert(actual_length == plr->attribute_block.length);
    free(quoted);
  }
}

/************************************************************************//**
  Save player (client) attributes data.
****************************************************************************/
static void sg_save_player_attributes(struct savedata *saving,
                                      struct player *plr)
{
  int plrno = player_number(plr);

  /* Check status and return if not OK (sg_success FALSE). */
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

/************************************************************************//**
  Load vision data
****************************************************************************/
static void sg_load_player_vision(struct loaddata *loading,
                                  struct player *plr)
{
  int plrno = player_number(plr);
  int total_ncities
    = secfile_lookup_int_default(loading->file, -1,
                                 "player%d.dc_total", plrno);
  int i;
  bool someone_alive = FALSE;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  if (game.server.revealmap & REVEAL_MAP_DEAD) {
    player_list_iterate(team_members(plr->team), pteam_member) {
      if (pteam_member->is_alive) {
        someone_alive = TRUE;
        break;
      }
    } player_list_iterate_end;

    if (!someone_alive) {
      /* Reveal all for completely dead teams. */
      map_know_and_see_all(plr);
    }
  }

  if (-1 == total_ncities
      || !game.info.fogofwar
      || !secfile_lookup_bool_default(loading->file, TRUE,
                                      "game.save_private_map")) {
    /* We have:
     * - a dead player;
     * - fogged cities are not saved for any reason;
     * - a savegame with fog of war turned off;
     * - or game.save_private_map is not set to FALSE in the scenario /
     * savegame. The players private knowledge is set to be what they could
     * see without fog of war. */
    whole_map_iterate(&(wld.map), ptile) {
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

  /* Load player map (extras). */
  halfbyte_iterate_extras(j, loading->extra.size) {
    LOAD_MAP_CHAR(ch, ptile,
                  sg_extras_set_dbv(&(map_get_player_tile(ptile, plr)->extras),
                                    ch, loading->extra.order + 4 * j),
                  loading->file, "player%d.map_e%02d_%04d", plrno, j);
  } halfbyte_iterate_extras_end;

  whole_map_iterate(&(wld.map), ptile) {
    struct player_tile *plrtile = map_get_player_tile(ptile, plr);
    bool regr_warn = FALSE;

    extra_type_by_cause_iterate(EC_RESOURCE, pres) {
      int pres_id = extra_number(pres);

      if (BV_ISSET(plrtile->extras, pres_id)) {
        if (plrtile->terrain == nullptr) {
          if (!regr_warn) {
            sg_regr(3030000, "FoW tile (%d, %d) has extras, though it's on unknown.",
                    TILE_XY(ptile));
            regr_warn = TRUE;
          }
          BV_CLR(plrtile->extras, pres_id);
        } else {
          plrtile->resource = pres;
          if (!terrain_has_resource(plrtile->terrain, pres)) {
            BV_CLR(plrtile->extras, pres_id);
          }
        }
      }
    } extra_type_by_cause_iterate_end;
  } whole_map_iterate_end;

  if (game.server.foggedborders) {
    /* Load player map (border). */
    int x, y;

    for (y = 0; y < MAP_NATIVE_HEIGHT; y++) {
      const char *buffer
        = secfile_lookup_str(loading->file, "player%d.map_owner%04d",
                             plrno, y);
      const char *buffer2
        = secfile_lookup_str(loading->file, "player%d.extras_owner%04d",
                             plrno, y);
      const char *ptr = buffer;
      const char *ptr2 = buffer2;

      sg_failure_ret(NULL != buffer,
                    "Savegame corrupt - map line %d not found.", y);
      for (x = 0; x < MAP_NATIVE_WIDTH; x++) {
        char token[TOKEN_SIZE];
        char token2[TOKEN_SIZE];
        int number;
        struct tile *ptile = native_pos_to_tile(&(wld.map), x, y);

        scanin(&ptr, ",", token, sizeof(token));
        sg_failure_ret('\0' != token[0],
                       "Savegame corrupt - map size not correct.");
        if (strcmp(token, "-") == 0) {
          map_get_player_tile(ptile, plr)->owner = NULL;
        } else  {
          sg_failure_ret(str_to_int(token, &number),
                         "Savegame corrupt - got tile owner=%s in (%d, %d).",
                         token, x, y);
          map_get_player_tile(ptile, plr)->owner = player_by_number(number);
        }

        scanin(&ptr2, ",", token2, sizeof(token2));
        sg_failure_ret('\0' != token2[0],
                       "Savegame corrupt - map size not correct.");
        if (strcmp(token2, "-") == 0) {
          map_get_player_tile(ptile, plr)->extras_owner = NULL;
        } else  {
          sg_failure_ret(str_to_int(token2, &number),
                         "Savegame corrupt - got extras owner=%s in (%d, %d).",
                         token, x, y);
          map_get_player_tile(ptile, plr)->extras_owner = player_by_number(number);
        }
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
  whole_map_iterate(&(wld.map), ptile) {
    if (map_is_known_and_seen(ptile, plr, V_MAIN)) {
      struct city *pcity = tile_city(ptile);

      update_player_tile_knowledge(plr, ptile);
      reality_check_city(plr, ptile);

      if (NULL != pcity) {
        update_dumb_city(plr, pcity);
      }
    } else if (!game.server.foggedborders && map_is_known(ptile, plr)) {
      /* Non fogged borders aren't loaded. See hrm Bug #879084 */
      struct player_tile *plrtile = map_get_player_tile(ptile, plr);

      plrtile->owner = tile_owner(ptile);
    }
  } whole_map_iterate_end;
}

/************************************************************************//**
  Load data for one seen city. sg_save_player_vision_city() is not defined.
****************************************************************************/
static bool sg_load_player_vision_city(struct loaddata *loading,
                                       struct player *plr,
                                       struct vision_site *pdcity,
                                       const char *citystr)
{
  const char *str;
  int i, id, size;
  citizens city_size;
  int nat_x, nat_y;
  const char *stylename;
  enum capital_type cap;
  const char *vname;

  sg_warn_ret_val(secfile_lookup_int(loading->file, &nat_x, "%s.x",
                                     citystr),
                  FALSE, "%s", secfile_error());
  sg_warn_ret_val(secfile_lookup_int(loading->file, &nat_y, "%s.y",
                                     citystr),
                  FALSE, "%s", secfile_error());
  pdcity->location = native_pos_to_tile(&(wld.map), nat_x, nat_y);
  sg_warn_ret_val(NULL != pdcity->location, FALSE,
                  "%s invalid tile (%d,%d)", citystr, nat_x, nat_y);

  sg_warn_ret_val(secfile_lookup_int(loading->file, &id, "%s.owner",
                                     citystr),
                  FALSE, "%s", secfile_error());
  pdcity->owner = player_by_number(id);
  sg_warn_ret_val(NULL != pdcity->owner, FALSE,
                  "%s has invalid owner (%d); skipping.", citystr, id);

  sg_warn_ret_val(secfile_lookup_int(loading->file, &id, "%s.original",
                                     citystr),
                  FALSE, "%s", secfile_error());
  if (id >= 0) {
    pdcity->original = player_by_number(id);
    sg_warn_ret_val(NULL != pdcity->original, FALSE,
                    "%s has invalid original owner (%d); skipping.", citystr, id);
  } else {
    pdcity->original = nullptr;
  }

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
  str = secfile_lookup_str(loading->file, "%s.improvements", citystr);
  sg_warn_ret_val(str != NULL, FALSE, "%s", secfile_error());
  sg_warn_ret_val(strlen(str) == loading->improvement.size, FALSE,
                  "Invalid length of '%s.improvements' ("
                  SIZE_T_PRINTF " ~= " SIZE_T_PRINTF ").",
                  citystr, strlen(str), loading->improvement.size);
  for (i = 0; i < loading->improvement.size; i++) {
    sg_warn_ret_val(str[i] == '1' || str[i] == '0', FALSE,
                    "Undefined value '%c' within '%s.improvements'.",
                    str[i], citystr)

    if (str[i] == '1') {
      struct impr_type *pimprove =
          improvement_by_rule_name(loading->improvement.order[i]);
      if (pimprove) {
        BV_SET(pdcity->improvements, improvement_index(pimprove));
      }
    }
  }

  vname = secfile_lookup_str_default(loading->file, NULL,
                                     "%s.name", citystr);

  if (vname != NULL) {
    pdcity->name = fc_strdup(vname);
  }

  pdcity->occupied = secfile_lookup_bool_default(loading->file, FALSE,
                                                 "%s.occupied", citystr);
  pdcity->walls = secfile_lookup_bool_default(loading->file, FALSE,
                                              "%s.walls", citystr);
  pdcity->happy = secfile_lookup_bool_default(loading->file, FALSE,
                                              "%s.happy", citystr);
  pdcity->unhappy = secfile_lookup_bool_default(loading->file, FALSE,
                                                "%s.unhappy", citystr);
  stylename = secfile_lookup_str_default(loading->file, NULL,
                                             "%s.style", citystr);
  if (stylename != NULL) {
    pdcity->style = city_style_by_rule_name(stylename);
  } else {
    pdcity->style = 0;
  }
  if (pdcity->style < 0) {
    pdcity->style = 0;
  }

  pdcity->city_image = secfile_lookup_int_default(loading->file, -100,
                                                  "%s.city_image", citystr);

  cap = capital_type_by_name(secfile_lookup_str_default(loading->file, NULL,
                                                        "%s.capital", citystr),
                             fc_strcasecmp);

  if (capital_type_is_valid(cap)) {
    pdcity->capital = cap;
  } else {
    pdcity->capital = CAPITAL_NOT;
  }

  return TRUE;
}

/************************************************************************//**
  Save vision data
****************************************************************************/
static void sg_save_player_vision(struct savedata *saving,
                                  struct player *plr)
{
  int i, plrno = player_number(plr);

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  if (!game.info.fogofwar || !game.server.save_options.save_private_map) {
    /* The player can see all, there's no reason to save the private map. */
    return;
  }

  /* Save the map (terrain). */
  SAVE_MAP_CHAR(ptile,
                terrain2char(map_get_player_tile(ptile, plr)->terrain),
                saving->file, "player%d.map_t%04d", plrno);

  if (game.server.foggedborders) {
    /* Save the map (borders). */
    int x, y;

    for (y = 0; y < MAP_NATIVE_HEIGHT; y++) {
      char line[MAP_NATIVE_WIDTH * TOKEN_SIZE];

      line[0] = '\0';
      for (x = 0; x < MAP_NATIVE_WIDTH; x++) {
        char token[TOKEN_SIZE];
        struct tile *ptile = native_pos_to_tile(&(wld.map), x, y);
        struct player_tile *plrtile = map_get_player_tile(ptile, plr);

        if (plrtile == NULL || plrtile->owner == NULL) {
          strcpy(token, "-");
        } else {
          fc_snprintf(token, sizeof(token), "%d",
                      player_number(plrtile->owner));
        }
        strcat(line, token);
        if (x < MAP_NATIVE_WIDTH) {
          strcat(line, ",");
        }
      }
      secfile_insert_str(saving->file, line, "player%d.map_owner%04d",
                         plrno, y);
    }

    for (y = 0; y < MAP_NATIVE_HEIGHT; y++) {
      char line[MAP_NATIVE_WIDTH * TOKEN_SIZE];

      line[0] = '\0';
      for (x = 0; x < MAP_NATIVE_WIDTH; x++) {
        char token[TOKEN_SIZE];
        struct tile *ptile = native_pos_to_tile(&(wld.map), x, y);
        struct player_tile *plrtile = map_get_player_tile(ptile, plr);

        if (plrtile == NULL || plrtile->extras_owner == NULL) {
          strcpy(token, "-");
        } else {
          fc_snprintf(token, sizeof(token), "%d",
                      player_number(plrtile->extras_owner));
        }
        strcat(line, token);
        if (x < MAP_NATIVE_WIDTH) {
          strcat(line, ",");
        }
      }
      secfile_insert_str(saving->file, line, "player%d.extras_owner%04d",
                         plrno, y);
    }
  }

  /* Save the map (extras). */
  halfbyte_iterate_extras(j, game.control.num_extra_types) {
    int mod[4];
    int l;

    for (l = 0; l < 4; l++) {
      if (4 * j + 1 > game.control.num_extra_types) {
        mod[l] = -1;
      } else {
        mod[l] = 4 * j + l;
      }
    }

    SAVE_MAP_CHAR(ptile,
                  sg_extras_get_dbv(&(map_get_player_tile(ptile, plr)->extras),
                                    map_get_player_tile(ptile, plr)->resource,
                                    mod),
                  saving->file, "player%d.map_e%02d_%04d", plrno, j);
  } halfbyte_iterate_extras_end;

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
  whole_map_iterate(&(wld.map), ptile) {
    struct vision_site *pdcity = map_get_player_city(ptile, plr);
    char impr_buf[B_LAST + 1];
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
      if (pdcity->original != nullptr) {
        secfile_insert_int(saving->file, player_number(pdcity->original),
                           "%s.original", buf);
      } else {
        secfile_insert_int(saving->file, -1, "%s.original", buf);
      }

      secfile_insert_int(saving->file, vision_site_size_get(pdcity),
                         "%s.size", buf);
      secfile_insert_bool(saving->file, pdcity->occupied,
                          "%s.occupied", buf);
      secfile_insert_bool(saving->file, pdcity->walls, "%s.walls", buf);
      secfile_insert_bool(saving->file, pdcity->happy, "%s.happy", buf);
      secfile_insert_bool(saving->file, pdcity->unhappy, "%s.unhappy", buf);
      secfile_insert_str(saving->file, city_style_rule_name(pdcity->style),
                         "%s.style", buf);
      secfile_insert_int(saving->file, pdcity->city_image, "%s.city_image", buf);
      secfile_insert_str(saving->file, capital_type_name(pdcity->capital),
                         "%s.capital", buf);

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
                     SIZE_T_PRINTF " < " SIZE_T_PRINTF" ).",
                     buf, strlen(impr_buf), sizeof(impr_buf));
      secfile_insert_str(saving->file, impr_buf, "%s.improvements", buf);
      if (pdcity->name != NULL) {
        secfile_insert_str(saving->file, pdcity->name, "%s.name", buf);
      }

      i++;
    }
  } whole_map_iterate_end;

  secfile_insert_int(saving->file, i, "player%d.dc_total", plrno);
}

/* =======================================================================
 * Load / save the researches.
 * ======================================================================= */

/************************************************************************//**
  Load '[research]'.
****************************************************************************/
static void sg_load_researches(struct loaddata *loading)
{
  struct research *presearch;
  int count;
  int number;
  const char *str;
  int i, j;
  int *vlist_research;

  vlist_research = NULL;
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  /* Initialize all researches. */
  researches_iterate(pinitres) {
    init_tech(pinitres, FALSE);
  } researches_iterate_end;

  /* May be unsaved (e.g. scenario case). */
  count = secfile_lookup_int_default(loading->file, 0, "research.count");
  for (i = 0; i < count; i++) {
    sg_failure_ret(secfile_lookup_int(loading->file, &number,
                                      "research.r%d.number", i),
                   "%s", secfile_error());
    presearch = research_by_number(number);
    sg_failure_ret(presearch != NULL,
                   "Invalid research number %d in 'research.r%d.number'",
                   number, i);

    presearch->tech_goal = technology_load(loading->file,
                                           "research.r%d.goal", i);
    sg_failure_ret(secfile_lookup_int(loading->file,
                                      &presearch->future_tech,
                                      "research.r%d.futuretech", i),
                   "%s", secfile_error());
    sg_failure_ret(secfile_lookup_int(loading->file,
                                      &presearch->bulbs_researched,
                                      "research.r%d.bulbs", i),
                   "%s", secfile_error());
    sg_failure_ret(secfile_lookup_int(loading->file,
                                      &presearch->bulbs_researching_saved,
                                      "research.r%d.bulbs_before", i),
                   "%s", secfile_error());
    presearch->researching_saved = technology_load(loading->file,
                                                   "research.r%d.saved", i);
    presearch->researching = technology_load(loading->file,
                                             "research.r%d.now", i);
    sg_failure_ret(secfile_lookup_int(loading->file,
                                      &presearch->free_bulbs,
                                      "research.r%d.free_bulbs", i),
                   "%s", secfile_error());

    str = secfile_lookup_str(loading->file, "research.r%d.done", i);
    sg_failure_ret(str != NULL, "%s", secfile_error());
    sg_failure_ret(strlen(str) == loading->technology.size,
                   "Invalid length of 'research.r%d.done' ("
                   SIZE_T_PRINTF " ~= " SIZE_T_PRINTF ").",
                   i, strlen(str), loading->technology.size);
    for (j = 0; j < loading->technology.size; j++) {
      sg_failure_ret(str[j] == '1' || str[j] == '0',
                     "Undefined value '%c' within 'research.r%d.done'.",
                     str[j], i);

      if (str[j] == '1') {
        struct advance *padvance
          = advance_by_rule_name(loading->technology.order[j]);

        if (padvance) {
          research_invention_set(presearch, advance_number(padvance),
                                 TECH_KNOWN);
        }
      }
    }

    if (game.server.multiresearch) {
      size_t count_res;
      int tn;

      vlist_research = secfile_lookup_int_vec(loading->file, &count_res,
                                              "research.r%d.vbs", i);

      for (tn = 0; tn < count_res; tn++) {
        struct advance *padvance = advance_by_rule_name(loading->technology.order[tn]);

        if (padvance != NULL) {
          presearch->inventions[advance_index(padvance)].bulbs_researched_saved
            = vlist_research[tn];
        }
      }
    }
  }

  /* In case of tech_leakage, we can update research only after all the
   * researches have been loaded */
  researches_iterate(pupres) {
    research_update(pupres);
  } researches_iterate_end;
}

/************************************************************************//**
  Save '[research]'.
****************************************************************************/
static void sg_save_researches(struct savedata *saving)
{
  char invs[A_LAST];
  int i = 0;
  int *vlist_research;

  vlist_research = NULL;
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  if (saving->save_players) {
    researches_iterate(presearch) {
      secfile_insert_int(saving->file, research_number(presearch),
                         "research.r%d.number", i);
      technology_save(saving->file, "research.r%d.goal",
                      i, presearch->tech_goal);
      secfile_insert_int(saving->file, presearch->future_tech,
                         "research.r%d.futuretech", i);
      secfile_insert_int(saving->file, presearch->bulbs_researching_saved,
                         "research.r%d.bulbs_before", i);
      if (game.server.multiresearch) {
        vlist_research = fc_calloc(game.control.num_tech_types, sizeof(int));
        advance_index_iterate(A_FIRST, j) {
          vlist_research[j] = presearch->inventions[j].bulbs_researched_saved;
        } advance_index_iterate_end;
        secfile_insert_int_vec(saving->file, vlist_research,
                               game.control.num_tech_types,
                               "research.r%d.vbs", i);
        if (vlist_research) {
          free(vlist_research);
        }
      }
      technology_save(saving->file, "research.r%d.saved",
                      i, presearch->researching_saved);
      secfile_insert_int(saving->file, presearch->bulbs_researched,
                         "research.r%d.bulbs", i);
      technology_save(saving->file, "research.r%d.now",
                      i, presearch->researching);
      secfile_insert_int(saving->file, presearch->free_bulbs,
                         "research.r%d.free_bulbs", i);
      /* Save technology lists as bytevector. Note that technology order is
       * saved in savefile.technology.order */
      advance_index_iterate(A_NONE, tech_id) {
        invs[tech_id] = (valid_advance_by_number(tech_id) != NULL
                         && research_invention_state(presearch, tech_id)
                            == TECH_KNOWN ? '1' : '0');
      } advance_index_iterate_end;
      invs[game.control.num_tech_types] = '\0';
      secfile_insert_str(saving->file, invs, "research.r%d.done", i);
      i++;
    } researches_iterate_end;
    secfile_insert_int(saving->file, i, "research.count");
  }
}

/* =======================================================================
 * Load / save the event cache. Should be the last thing to do.
 * ======================================================================= */

/************************************************************************//**
  Load '[event_cache]'.
****************************************************************************/
static void sg_load_event_cache(struct loaddata *loading)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  event_cache_load(loading->file, "event_cache");
}

/************************************************************************//**
  Save '[event_cache]'.
****************************************************************************/
static void sg_save_event_cache(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  if (saving->scenario) {
    /* Do _not_ save events in a scenario. */
    return;
  }

  event_cache_save(saving->file, "event_cache");
}

/* =======================================================================
 * Load / save the open treaties
 * ======================================================================= */

/************************************************************************//**
  Load '[treaty_xxx]'.
****************************************************************************/
static void sg_load_treaties(struct loaddata *loading)
{
  int tidx;
  const char *plr0;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  for (tidx = 0; (plr0 = secfile_lookup_str_default(loading->file, NULL,
                                                    "treaty%d.plr0", tidx)) != NULL ;
       tidx++) {
    const char *plr1;
    const char *ct;
    int cidx;
    struct player *p0, *p1;

    plr1 = secfile_lookup_str(loading->file, "treaty%d.plr1", tidx);

    p0 = player_by_name(plr0);
    p1 = player_by_name(plr1);

    if (p0 == NULL || p1 == NULL) {
      log_error("Treaty between unknown players %s and %s", plr0, plr1);
    } else {
      struct treaty *ptreaty = fc_malloc(sizeof(*ptreaty));

      init_treaty(ptreaty, p0, p1);
      treaty_add(ptreaty);

      for (cidx = 0; (ct = secfile_lookup_str_default(loading->file, NULL,
                                                      "treaty%d.clause%d.type",
                                                      tidx, cidx)) != NULL ;
           cidx++ ) {
        enum clause_type type = clause_type_by_name(ct, fc_strcasecmp);
        const char *plrx;

        if (!clause_type_is_valid(type)) {
          log_error("Invalid clause type \"%s\"", ct);
        } else {
          struct player *pgiver = NULL;

          plrx = secfile_lookup_str(loading->file, "treaty%d.clause%d.from",
                                    tidx, cidx);

          if (!fc_strcasecmp(plrx, plr0)) {
            pgiver = p0;
          } else if (!fc_strcasecmp(plrx, plr1)) {
            pgiver = p1;
          } else {
            log_error("Clause giver %s is not participant of the treaty"
                      "between %s and %s", plrx, plr0, plr1);
          }

          if (pgiver != NULL) {
            int value;

            value = secfile_lookup_int_default(loading->file, 0,
                                               "treaty%d.clause%d.value",
                                               tidx, cidx);

            add_clause(ptreaty, pgiver, type, value, NULL);
          }
        }
      }

      /* These must be after clauses have been added so that acceptance
       * does not get cleared by what seems like changes to the treaty. */
      ptreaty->accept0 = secfile_lookup_bool_default(loading->file, FALSE,
                                                     "treaty%d.accept0", tidx);
      ptreaty->accept1 = secfile_lookup_bool_default(loading->file, FALSE,
                                                     "treaty%d.accept1", tidx);
    }
  }
}

typedef struct {
  int tidx;
  struct section_file *file;
} treaty_cb_data;

/************************************************************************//**
  Save '[treaty_xxx]'.
****************************************************************************/
static void treaty_save(struct treaty *ptr, void *data_in)
{
  char tpath[512];
  int cidx = 0;
  treaty_cb_data *data = (treaty_cb_data *)data_in;

  fc_snprintf(tpath, sizeof(tpath), "treaty%d", data->tidx++);

  secfile_insert_str(data->file, player_name(ptr->plr0), "%s.plr0", tpath);
  secfile_insert_str(data->file, player_name(ptr->plr1), "%s.plr1", tpath);
  secfile_insert_bool(data->file, ptr->accept0, "%s.accept0", tpath);
  secfile_insert_bool(data->file, ptr->accept1, "%s.accept1", tpath);

  clause_list_iterate(ptr->clauses, pclaus) {
    char cpath[512];

    fc_snprintf(cpath, sizeof(cpath), "%s.clause%d", tpath, cidx++);

    secfile_insert_str(data->file, clause_type_name(pclaus->type), "%s.type", cpath);
    secfile_insert_str(data->file, player_name(pclaus->from), "%s.from", cpath);
    secfile_insert_int(data->file, pclaus->value, "%s.value", cpath);
  } clause_list_iterate_end;
}

/************************************************************************//**
  Save all treaties.
****************************************************************************/
static void sg_save_treaties(struct savedata *saving)
{
  treaty_cb_data data = { .tidx = 0, .file = saving->file };

  treaties_iterate(treaty_save, &data);
}

/* =======================================================================
 * Load / save the history report
 * ======================================================================= */

/************************************************************************//**
  Load '[history]'.
****************************************************************************/
static void sg_load_history(struct loaddata *loading)
{
  struct history_report *hist = history_report_get();
  int turn;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  turn = secfile_lookup_int_default(loading->file, -2, "history.turn");

  if (turn != -2) {
    hist->turn = turn;
  }

  if (turn + 1 >= game.info.turn) {
    const char *str;

    str = secfile_lookup_str(loading->file, "history.title");
    sg_failure_ret(str != NULL, "%s", secfile_error());
    sz_strlcpy(hist->title, str);
    str = secfile_lookup_str(loading->file, "history.body");
    sg_failure_ret(str != NULL, "%s", secfile_error());
    sz_strlcpy(hist->body, str);
  }
}

/************************************************************************//**
  Save '[history]'.
****************************************************************************/
static void sg_save_history(struct savedata *saving)
{
  struct history_report *hist = history_report_get();

  secfile_insert_int(saving->file, hist->turn, "history.turn");

  if (hist->turn + 1 >= game.info.turn) {
    secfile_insert_str(saving->file, hist->title, "history.title");
    secfile_insert_str(saving->file, hist->body, "history.body");
  }
}

/* =======================================================================
 * Load / save the mapimg definitions.
 * ======================================================================= */

/************************************************************************//**
  Load '[mapimg]'.
****************************************************************************/
static void sg_load_mapimg(struct loaddata *loading)
{
  int mapdef_count, i;

  /* Check status and return if not OK (sg_success FALSE). */
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

/************************************************************************//**
  Save '[mapimg]'.
****************************************************************************/
static void sg_save_mapimg(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success FALSE). */
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

/************************************************************************//**
  Sanity check for loaded game.
****************************************************************************/
static void sg_load_sanitycheck(struct loaddata *loading)
{
  int players;

  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();

  if (game.info.is_new_game) {
    /* Nothing to do for new games (or not started scenarios). */
    return;
  }

  /* Old savegames may have maxplayers lower than current player count,
   * fix. */
  players = normal_player_count();
  if (game.server.max_players < players) {
    log_verbose("Max players lower than current players, fixing");
    game.server.max_players = players;
  }

  /* Fix ferrying sanity */
  players_iterate(pplayer) {
    unit_list_iterate_safe(pplayer->units, punit) {
      if (!unit_transport_get(punit)
          && !can_unit_exist_at_tile(&(wld.map), punit, unit_tile(punit))) {
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
  } players_iterate_end;

  /* Recalculate the potential buildings for each city. Has caused some
   * problems with game random state.
   * This also changes the game state if you save the game directly after
   * loading it and compare the results. */
  players_iterate(pplayer) {
    /* Building advisor needs data phase open in order to work */
    adv_data_phase_init(pplayer, FALSE);
    building_advisor(pplayer);
    /* Close data phase again so it can be opened again when game starts. */
    adv_data_phase_done(pplayer);
  } players_iterate_end;

  /* Prevent a buggy or intentionally crafted save game from crashing
   * Freeciv. See hrm Bug #887748 */
  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      worker_task_list_iterate(pcity->task_reqs, ptask) {
        if (!worker_task_is_sane(ptask)) {
          log_error("[city id: %d] Bad worker task %d.",
                    pcity->id, ptask->act);
          worker_task_list_remove(pcity->task_reqs, ptask);
          free(ptask);
          ptask = NULL;
        }
      } worker_task_list_iterate_end;
    } city_list_iterate_end;
  } players_iterate_end;

  /* Check worked tiles map */
#ifdef FREECIV_DEBUG
  if (loading->worked_tiles != NULL) {
    /* check the entire map for unused worked tiles */
    whole_map_iterate(&(wld.map), ptile) {
      if (loading->worked_tiles[ptile->index] != -1) {
        log_error("[city id: %d] Unused worked tile at (%d, %d).",
                  loading->worked_tiles[ptile->index], TILE_XY(ptile));
      }
    } whole_map_iterate_end;
  }
#endif /* FREECIV_DEBUG */

  /* Check researching technologies and goals. */
  researches_iterate(presearch) {
    if (presearch->researching != A_UNSET
        && !is_future_tech(presearch->researching)
        && (valid_advance_by_number(presearch->researching) == NULL
            || (research_invention_state(presearch, presearch->researching)
                != TECH_PREREQS_KNOWN))) {
      log_sg(_("%s had invalid researching technology."),
             research_name_translation(presearch));
      presearch->researching = A_UNSET;
    }
    if (presearch->tech_goal != A_UNSET
        && !is_future_tech(presearch->tech_goal)
        && (valid_advance_by_number(presearch->tech_goal) == NULL
            || !research_invention_reachable(presearch, presearch->tech_goal)
            || (research_invention_state(presearch, presearch->tech_goal)
                == TECH_KNOWN))) {
      log_sg(_("%s had invalid technology goal."),
             research_name_translation(presearch));
      presearch->tech_goal = A_UNSET;
    }

    presearch->techs_researched = recalculate_techs_researched(presearch);
  } researches_iterate_end;

  /* Check if some player has more than one of some UTYF_UNIQUE unit type */
  players_iterate(pplayer) {
    int unique_count[U_LAST];

    memset(unique_count, 0, sizeof(unique_count));

    unit_list_iterate(pplayer->units, punit) {
      unique_count[utype_index(unit_type_get(punit))]++;
    } unit_list_iterate_end;

    unit_type_iterate(ut) {
      if (unique_count[utype_index(ut)] > 1 && utype_has_flag(ut, UTYF_UNIQUE)) {
        log_sg(_("%s has multiple units of type %s though it should be possible "
                 "to have only one."),
               player_name(pplayer), utype_name_translation(ut));
      }
    } unit_type_iterate_end;
  } players_iterate_end;

  players_iterate(pplayer) {
    unit_list_iterate_safe(pplayer->units, punit) {
      if (punit->has_orders
          && !unit_order_list_is_sane(&(wld.map), punit->orders.length,
                                      punit->orders.list)) {
        log_sg("Invalid unit orders for unit %d.", punit->id);
        free_unit_orders(punit);
      }
    } unit_list_iterate_safe_end;
  } players_iterate_end;

  /* Check max rates (rules may have changed since saving) */
  players_iterate(pplayer) {
    player_limit_to_max_rates(pplayer);
  } players_iterate_end;

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
  }

  /* At the end do the default sanity checks. */
  sanity_check();
}

/************************************************************************//**
  Sanity check for saved game.
****************************************************************************/
static void sg_save_sanitycheck(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success FALSE). */
  sg_check_ret();
}
