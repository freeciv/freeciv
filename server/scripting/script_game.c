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
  Parse and execute the script in str
**************************************************************************/
bool script_do_string(const char *str)
{
  lua_State *state = state_lua;
  int status = luascript_do_string(state, str, "cmd");
  return (status == 0);
}

/**************************************************************************
  Parse and execute the script at filename.
**************************************************************************/
bool script_do_file(const char *filename)
{
  lua_State *state = state_lua;
  int status = luascript_do_file(state, filename);
  return (status == 0);
}

/****************************************************************************
  Mark any, if exported, full userdata representing 'object' in
  the current script state as 'Nonexistent'.
  This changes the type of the lua variable.
****************************************************************************/
void script_remove_exported_object(void *object)
{
  lua_State *state = state_lua;
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
  lua_State *state = state_lua;
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
  lua_State *state = state_lua;
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
  lua_State *state = state_lua;
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
  lua_State *state = state_lua;
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

  /* Set the status to the global variable. */
  state_lua = state;

  return TRUE;
}

/**************************************************************************
  Free the scripting data.
**************************************************************************/
void script_free(void)
{
  lua_State *state = state_lua;
  if (state) {
    script_code_free();
    script_vars_free();

    script_signals_free();

    luascript_destroy(state);
    state = NULL;
    state_lua = NULL;
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
