/**********************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Project
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
#include "registry.h"

/* server/scripting */
#include "api_specenum.h"
#include "luascript.h"
#include "script_signal.h"
#include "tolua_common_a_gen.h"
#include "tolua_common_z_gen.h"
#include "tolua_game_gen.h"
#include "tolua_server_gen.h"

#include "script_game.h"

/**************************************************************************
  Lua virtual machine state.
**************************************************************************/
static lua_State *state = NULL;

/**************************************************************************
  Optional game script code (useful for scenarios).
**************************************************************************/
static char *script_code = NULL;

static void script_vars_init(void);
static void script_vars_free(void);
static void script_vars_load(struct section_file *file);
static void script_vars_save(struct section_file *file);
static void script_code_init(void);
static void script_code_free(void);
static void script_code_load(struct section_file *file);
static void script_code_save(struct section_file *file);

/**************************************************************************
  Internal api error function.
  Invoking this will cause Lua to stop executing the current context and
  throw an exception, so to speak.
**************************************************************************/
int script_error(const char *format, ...)
{
  va_list vargs;
  int ret;

  va_start(vargs, format);
  ret = luascript_error(state, format, vargs);
  va_end(vargs);

  return ret;
}

/**************************************************************************
  Like script_error, but using a prefix identifying the called lua function:
    bad argument #narg to '<func>': msg
**************************************************************************/
int script_arg_error(int narg, const char *msg)
{
  return luascript_arg_error(state, narg, msg);
}

/**************************************************************************
  Parse and execute the script in str
**************************************************************************/
bool script_do_string(const char *str)
{
  int status = luascript_do_string(state, str, "cmd");
  return (status == 0);
}

/**************************************************************************
  Parse and execute the script at filename.
**************************************************************************/
bool script_do_file(const char *filename)
{
  int status = luascript_do_file(state, filename);
  return (status == 0);
}

/****************************************************************************
  Invoke the 'callback_name' Lua function.
****************************************************************************/
bool script_callback_invoke(const char *callback_name, int nargs,
                            enum api_types *parg_types, va_list args)
{
  bool stop_emission = FALSE;

  /* The function name */
  lua_getglobal(state, callback_name);

  if (!lua_isfunction(state, -1)) {
    log_error("lua error: Unknown callback '%s'", callback_name);
    lua_pop(state, 1);
    return FALSE;
  }

  luascript_push_args(state, nargs, parg_types, args);

  /* Call the function with nargs arguments, return 1 results */
  if (luascript_call(state, nargs, 1, NULL)) {
    return FALSE;
  }

  /* Shall we stop the emission of this signal? */
  if (lua_isboolean(state, -1)) {
    stop_emission = lua_toboolean(state, -1);
  }
  lua_pop(state, 1);   /* pop return value */

  return stop_emission;
}

/****************************************************************************
  Mark any, if exported, full userdata representing 'object' in
  the current script state as 'Nonexistent'.
  This changes the type of the lua variable.
****************************************************************************/
void script_remove_exported_object(void *object)
{
  luascript_remove_exported_object(state, object);
}

/**************************************************************************
  Initialize the game script variables.
**************************************************************************/
static void script_vars_init(void)
{
  /* nothing */
}

/**************************************************************************
  Free the game script variables.
**************************************************************************/
static void script_vars_free(void)
{
  /* nothing */
}

/**************************************************************************
  Load the game script variables in file.
**************************************************************************/
static void script_vars_load(struct section_file *file)
{
  if (state) {
    const char *vars;
    const char *section = "script.vars";

    vars = secfile_lookup_str_default(file, "", "%s", section);
    luascript_do_string(state, vars, section);
  }
}

/**************************************************************************
  Save the game script variables to file.
**************************************************************************/
static void script_vars_save(struct section_file *file)
{
  if (state) {
    lua_getglobal(state, "_freeciv_state_dump");
    if (luascript_call(state, 0, 1, NULL) == 0) {
      const char *vars;

      vars = lua_tostring(state, -1);
      lua_pop(state, 1);

      if (vars) {
        secfile_insert_str_noescape(file, vars, "script.vars");
      }
    } else {
      /* _freeciv_state_dump in api.pkg is busted */
      log_error("lua error: Failed to dump variables");
    }
  }
}

/**************************************************************************
  Initialize the optional game script code (useful for scenarios).
**************************************************************************/
static void script_code_init(void)
{
  script_code = NULL;
}

/**************************************************************************
  Free the optional game script code (useful for scenarios).
**************************************************************************/
static void script_code_free(void)
{
  if (script_code) {
    free(script_code);
    script_code = NULL;
  }
}

/**************************************************************************
  Load the optional game script code from file (useful for scenarios).
**************************************************************************/
static void script_code_load(struct section_file *file)
{
  if (!script_code) {
    const char *code;
    const char *section = "script.code";

    code = secfile_lookup_str_default(file, "", "%s", section);
    script_code = fc_strdup(code);
    luascript_do_string(state, script_code, section);
  }
}

/**************************************************************************
  Save the optional game script code to file (useful for scenarios).
**************************************************************************/
static void script_code_save(struct section_file *file)
{
  if (script_code) {
    secfile_insert_str_noescape(file, script_code, "script.code");
  }
}

/**************************************************************************
  Initialize the scripting state.
**************************************************************************/
bool script_init(void)
{
  if (state != NULL) {
    return TRUE;
  }

  state = luascript_new();
  if (!state) {
    return FALSE;
  }

  tolua_common_a_open(state);
  api_specenum_open(state);
  tolua_game_open(state);
  tolua_server_open(state);
  tolua_common_z_open(state);

  script_code_init();
  script_vars_init();

  script_signals_init();

  return TRUE;
}

/**************************************************************************
  Free the scripting data.
**************************************************************************/
void script_free(void)
{
  if (state) {
    script_code_free();
    script_vars_free();

    script_signals_free();

    luascript_destroy(state);
    state = NULL;
  }
}

/**************************************************************************
  Load the scripting state from file.
**************************************************************************/
void script_state_load(struct section_file *file)
{
  script_code_load(file);

  /* Variables must be loaded after code is loaded and executed,
   * so we restore their saved state properly */
  script_vars_load(file);
}

/**************************************************************************
  Save the scripting state to file.
**************************************************************************/
void script_state_save(struct section_file *file)
{
  script_code_save(file);
  script_vars_save(file);
}
