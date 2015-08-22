/*****************************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

/* dependencies/lua */
#include "lua.h"
#include "lualib.h"

/* dependencies/tolua */
#include "tolua.h"

/* utility */
#include "astring.h"
#include "log.h"
#include "mem.h"
#include "registry.h"

/* common/scriptcore */
#include "api_game_specenum.h"
#include "luascript.h"
#include "luascript_signal.h"
#include "luascript_func.h"
#include "tolua_common_a_gen.h"
#include "tolua_common_z_gen.h"
#include "tolua_game_gen.h"
#include "tolua_signal_gen.h"

/* server */
#include "console.h"
#include "stdinhand.h"

/* server/scripting */
#include "tolua_server_gen.h"

#include "script_server.h"

/*****************************************************************************
  Lua virtual machine state.
*****************************************************************************/
static struct fc_lua *fcl_main = NULL;

/*****************************************************************************
  Optional game script code (useful for scenarios).
*****************************************************************************/
static char *script_server_code = NULL;

static void script_server_vars_init(void);
static void script_server_vars_free(void);
static void script_server_vars_load(struct section_file *file);
static void script_server_vars_save(struct section_file *file);
static void script_server_code_init(void);
static void script_server_code_free(void);
static void script_server_code_load(struct section_file *file);
static void script_server_code_save(struct section_file *file);

static void script_server_signal_create(void);
static void script_server_functions_define(void);

static void script_server_cmd_reply(struct fc_lua *fcl, enum log_level level,
                                    const char *format, ...)
            fc__attribute((__format__ (__printf__, 3, 4)));

/*****************************************************************************
  Parse and execute the script in str
*****************************************************************************/
bool script_server_do_string(struct connection *caller, const char *str)
{
  int status;
  struct connection *save_caller;
  luascript_log_func_t save_output_fct;

  /* Set a log callback function which allows to send the results of the
   * command to the clients. */
  save_caller = fcl_main->caller;
  save_output_fct = fcl_main->output_fct;
  fcl_main->output_fct = script_server_cmd_reply;
  fcl_main->caller = caller;

  status = luascript_do_string(fcl_main, str, "cmd");

  /* Reset the changes. */
  fcl_main->caller = save_caller;
  fcl_main->output_fct = save_output_fct;

  return (status == 0);
}

/*****************************************************************************
  Load script to a buffer
*****************************************************************************/
bool script_server_load_file(const char *filename, char **buf)
{
  FILE *ffile;
  struct stat stats;
  char *buffer;

  fc_stat(filename, &stats);
  ffile = fc_fopen(filename, "r");

  if (ffile != NULL) {
    int len;

    buffer = fc_malloc(stats.st_size + 1);

    len = fread(buffer, 1, stats.st_size, ffile);

    if (len == stats.st_size) {
      buffer[len] = '\0';

      *buf = buffer;
    }
    fclose(ffile);
  }

  return 1;
}  

/*****************************************************************************
  Parse and execute the script at filename.
*****************************************************************************/
bool script_server_do_file(struct connection *caller, const char *filename)
{
  int status = 1;
  struct connection *save_caller;
  luascript_log_func_t save_output_fct;

  /* Set a log callback function which allows to send the results of the
   * command to the clients. */
  save_caller = fcl_main->caller;
  save_output_fct = fcl_main->output_fct;
  fcl_main->output_fct = script_server_cmd_reply;
  fcl_main->caller = caller;

  status = luascript_do_file(fcl_main, filename);

  /* Reset the changes. */
  fcl_main->caller = save_caller;
  fcl_main->output_fct = save_output_fct;

  return (status == 0);
}

/*****************************************************************************
  Invoke the 'callback_name' Lua function.
*****************************************************************************/
bool script_server_callback_invoke(const char *callback_name, int nargs,
                                   enum api_types *parg_types, va_list args)
{
  return luascript_callback_invoke(fcl_main, callback_name, nargs, parg_types,
                                   args);
}

/*****************************************************************************
  Mark any, if exported, full userdata representing 'object' in
  the current script state as 'Nonexistent'.
  This changes the type of the lua variable.
*****************************************************************************/
void script_server_remove_exported_object(void *object)
{
  luascript_remove_exported_object(fcl_main, object);
}

/*****************************************************************************
  Initialize the game script variables.
*****************************************************************************/
static void script_server_vars_init(void)
{
  /* nothing */
}

/*****************************************************************************
  Free the game script variables.
*****************************************************************************/
static void script_server_vars_free(void)
{
  /* nothing */
}

/*****************************************************************************
  Load the game script variables in file.
*****************************************************************************/
static void script_server_vars_load(struct section_file *file)
{
  luascript_vars_load(fcl_main, file, "script.vars");
}

/*****************************************************************************
  Save the game script variables to file.
*****************************************************************************/
static void script_server_vars_save(struct section_file *file)
{
  luascript_vars_save(fcl_main, file, "script.vars");
}

/*****************************************************************************
  Initialize the optional game script code (useful for scenarios).
*****************************************************************************/
static void script_server_code_init(void)
{
  script_server_code = NULL;
}

/*****************************************************************************
  Free the optional game script code (useful for scenarios).
*****************************************************************************/
static void script_server_code_free(void)
{
  if (script_server_code) {
    free(script_server_code);
    script_server_code = NULL;
  }
}

/*****************************************************************************
  Load the optional game script code from file (useful for scenarios).
*****************************************************************************/
static void script_server_code_load(struct section_file *file)
{
  if (!script_server_code) {
    const char *code;
    const char *section = "script.code";

    code = secfile_lookup_str_default(file, "", "%s", section);
    script_server_code = fc_strdup(code);
    luascript_do_string(fcl_main, script_server_code, section);
  }
}

/*****************************************************************************
  Save the optional game script code to file (useful for scenarios).
*****************************************************************************/
static void script_server_code_save(struct section_file *file)
{
  if (script_server_code) {
    secfile_insert_str_noescape(file, script_server_code, "script.code");
  }
}

/*****************************************************************************
  Initialize the scripting state.
*****************************************************************************/
bool script_server_init(void)
{
  if (fcl_main != NULL) {
    fc_assert_ret_val(fcl_main->state != NULL, FALSE);

    return TRUE;
  }

  fcl_main = luascript_new(NULL);
  if (fcl_main == NULL) {
    luascript_destroy(fcl_main);
    fcl_main = NULL;

    return FALSE;
  }

  tolua_common_a_open(fcl_main->state);
  api_specenum_open(fcl_main->state);
  tolua_game_open(fcl_main->state);
  tolua_signal_open(fcl_main->state);
  tolua_server_open(fcl_main->state);
  tolua_common_z_open(fcl_main->state);

  script_server_code_init();
  script_server_vars_init();

  luascript_signal_init(fcl_main);
  script_server_signal_create();

  luascript_func_init(fcl_main);
  script_server_functions_define();

  return TRUE;
}

/*****************************************************************************
  Free the scripting data.
*****************************************************************************/
void script_server_free(void)
{
  if (fcl_main != NULL) {
    script_server_code_free();
    script_server_vars_free();

    /* luascript_signal_free() is called by luascript_destroy(). */
    luascript_destroy(fcl_main);
    fcl_main = NULL;
  }
}

/*****************************************************************************
  Load the scripting state from file.
*****************************************************************************/
void script_server_state_load(struct section_file *file)
{
  script_server_code_load(file);

  /* Variables must be loaded after code is loaded and executed,
   * so we restore their saved state properly */
  script_server_vars_load(file);
}

/*****************************************************************************
  Save the scripting state to file.
*****************************************************************************/
void script_server_state_save(struct section_file *file)
{
  script_server_code_save(file);
  script_server_vars_save(file);
}

/*****************************************************************************
  Invoke all the callback functions attached to a given signal.
*****************************************************************************/
void script_server_signal_emit(const char *signal_name, int nargs, ...)
{
  va_list args;

  va_start(args, nargs);
  luascript_signal_emit_valist(fcl_main, signal_name, nargs, args);
  va_end(args);
}

/*****************************************************************************
  Declare any new signal types you need here.
*****************************************************************************/
static void script_server_signal_create(void)
{
  luascript_signal_create(fcl_main, "turn_started", 2,
                          API_TYPE_INT, API_TYPE_INT);
  luascript_signal_create(fcl_main, "unit_moved", 3,
                          API_TYPE_UNIT, API_TYPE_TILE, API_TYPE_TILE);

  /* Includes all newly-built cities. */
  luascript_signal_create(fcl_main, "city_built", 1,
                          API_TYPE_CITY);

  luascript_signal_create(fcl_main, "city_growth", 2,
                          API_TYPE_CITY, API_TYPE_INT);

  /* Only includes units built in cities, for now. */
  luascript_signal_create(fcl_main, "unit_built", 2,
                          API_TYPE_UNIT, API_TYPE_CITY);
  luascript_signal_create(fcl_main, "building_built", 2,
                          API_TYPE_BUILDING_TYPE, API_TYPE_CITY);

  /* These can happen for various reasons; the third argument gives the
   * reason (a simple string identifier).  Example identifiers:
   * "pop_cost", "need_tech", "need_building", "need_special",
   * "need_terrain", "need_government", "need_nation", "never",
   * "unavailable". */
  luascript_signal_create(fcl_main, "unit_cant_be_built", 3,
                          API_TYPE_UNIT_TYPE, API_TYPE_CITY, API_TYPE_STRING);
  luascript_signal_create(fcl_main, "building_cant_be_built", 3,
                          API_TYPE_BUILDING_TYPE, API_TYPE_CITY,
                          API_TYPE_STRING);

  /* The third argument contains the source: "researched", "traded",
   * "stolen", "hut". */
  luascript_signal_create(fcl_main, "tech_researched", 3,
                          API_TYPE_TECH_TYPE, API_TYPE_PLAYER,
                          API_TYPE_STRING);

  /* First player is city owner, second is enemy. */
  luascript_signal_create(fcl_main, "city_destroyed", 3,
                          API_TYPE_CITY, API_TYPE_PLAYER, API_TYPE_PLAYER);
  luascript_signal_create(fcl_main, "city_lost", 3,
                          API_TYPE_CITY, API_TYPE_PLAYER, API_TYPE_PLAYER);

  luascript_signal_create(fcl_main, "hut_enter", 1,
                          API_TYPE_UNIT);

  luascript_signal_create(fcl_main, "unit_lost", 3,
                          API_TYPE_UNIT, API_TYPE_PLAYER, API_TYPE_STRING);

  luascript_signal_create(fcl_main, "disaster", 3,
                          API_TYPE_DISASTER, API_TYPE_CITY, API_TYPE_BOOL);

  luascript_signal_create(fcl_main, "achievement_gained", 3,
                          API_TYPE_ACHIEVEMENT, API_TYPE_PLAYER,
                          API_TYPE_BOOL);

  luascript_signal_create(fcl_main, "map_generated", 0);

  luascript_signal_create(fcl_main, "action_started_unit_unit", 3,
                          API_TYPE_ACTION,
                          API_TYPE_UNIT, API_TYPE_UNIT);

  luascript_signal_create(fcl_main, "action_started_unit_units", 3,
                          API_TYPE_ACTION,
                          API_TYPE_UNIT, API_TYPE_TILE);

  luascript_signal_create(fcl_main, "action_started_unit_city", 3,
                          API_TYPE_ACTION,
                          API_TYPE_UNIT, API_TYPE_CITY);

  luascript_signal_create(fcl_main, "action_started_unit_tile", 3,
                          API_TYPE_ACTION,
                          API_TYPE_UNIT, API_TYPE_TILE);
}

/*****************************************************************************
  Add server callback functions; these must be defined in the lua script
  '<rulesetdir>/script.lua':

  respawn_callback (optional):
    - callback lua function for the respawn command
*****************************************************************************/
static void script_server_functions_define(void)
{
  luascript_func_add(fcl_main, "respawn_callback", FALSE, 1,
                     API_TYPE_PLAYER);
}

/*****************************************************************************
  Call a lua function.
*****************************************************************************/
bool script_server_call(const char *func_name, int nargs, ...)
{
  bool success;
  int ret;

  va_list args;
  va_start(args, nargs);
  success = luascript_func_call_valist(fcl_main, func_name, &ret, nargs, args);
  va_end(args);

  if (!success) {
    log_error("Lua function '%s' not defined.", func_name);
    return FALSE;
  } else if (!ret) {
    log_error("Error executing lua function '%s'.", func_name);
    return FALSE;
  }

  return TRUE;
}

/*****************************************************************************
  Send the message via cmd_reply().
*****************************************************************************/
static void script_server_cmd_reply(struct fc_lua *fcl, enum log_level level,
                                    const char *format, ...)
{
  va_list args;
  enum rfc_status rfc_status = C_OK;
  char buf[1024];

  va_start(args, format);
  fc_vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

  switch (level) {
  case LOG_FATAL:
    /* Special case - will quit the server. */
    log_fatal("%s", buf);
    break;
  case LOG_ERROR:
    rfc_status = C_WARNING;
    break;
  case LOG_NORMAL:
    rfc_status = C_COMMENT;
    break;
  case LOG_VERBOSE:
    rfc_status = C_LOG_BASE;
    break;
  case LOG_DEBUG:
    rfc_status = C_DEBUG;
    break;
  }

  cmd_reply(CMD_LUA, fcl->caller, rfc_status, "%s", buf);
}
