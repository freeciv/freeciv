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
#include <config.h>
#endif

#include <stdarg.h>

#include "lua.h"
#include "lualib.h"
#include "tolua.h"

#include "log.h"
#include "registry.h"

#include "api_gen.h"
#include "api_types.h"
#include "script_signal.h"

#include "script.h"

/**************************************************************************
  Lua virtual machine state.
**************************************************************************/
static lua_State *state;

/**************************************************************************
  Optional game script code (useful for scenarios).
**************************************************************************/
static char *script_code;


/**************************************************************************
  Internal api error function.
  Invoking this will cause Lua to stop executing the current context and
  throw an exception, so to speak.
**************************************************************************/
int script_error(const char *fmt, ...)
{
  va_list argp;

  va_start(argp, fmt);
  luaL_where(state, 1);
  lua_pushvfstring(state, fmt, argp);
  va_end(argp);
  lua_concat(state, 2);

  return lua_error(state);
}

/**************************************************************************
  Invoke the 'callback_name' Lua function.
**************************************************************************/
void script_callback_invoke(const char *callback_name,
    			    bool *stop_emission, bool *remove_callback,
			    int nargs, va_list args)
{
  int i, nres;
  bool res;

  *stop_emission = FALSE;
  *remove_callback = FALSE;

  /* The function name */
  lua_getglobal(state, callback_name);

  for (i = 0; i < nargs; i++) {
    int type;

    type = va_arg(args, int);

    switch (type) {
      case API_TYPE_INT:
	{
	  int arg;

	  arg = va_arg(args, int);
	  tolua_pushnumber(state, (lua_Number)arg);
	}
	break;
      case API_TYPE_BOOL:
	{
	  int arg;

	  arg = va_arg(args, int);
	  tolua_pushboolean(state, (bool)arg);
	}
	break;
      case API_TYPE_STRING:
	{
	  const char *arg;

	  arg = va_arg(args, const char*);
	  tolua_pushstring(state, arg);
	}
	break;
      case API_TYPE_PLAYER:
	{
	  Player *arg;

	  arg = va_arg(args, Player*);
	  tolua_pushusertype(state, (void*)arg, "Player");
	}
	break;
      case API_TYPE_CITY:
	{
	  City *arg;

	  arg = va_arg(args, City*);
	  tolua_pushusertype(state, (void*)arg, "City");
	}
	break;
      case API_TYPE_UNIT:
	{
	  Unit *arg;

	  arg = va_arg(args, Unit*);
	  tolua_pushusertype(state, (void*)arg, "Unit");
	}
	break;
      case API_TYPE_TILE:
	{
	  Tile *arg;

	  arg = va_arg(args, Tile*);
	  tolua_pushusertype(state, (void*)arg, "Tile");
	}
	break;
    }
  }

  /* Call the function with nargs arguments, return 2 results */
  if (lua_pcall(state, nargs, 2, 0) != 0) {
    return;
  }

  nres = lua_gettop(state);

  switch (nres) {
    case 2:
      if (lua_isboolean(state, -1)) {
	res = lua_toboolean(state, -1);
	lua_pop(state, 1);

        /* Shall we remove this callback? */
	if (res) {
	  *remove_callback = TRUE;
	}
      }
    case 1:
      if (lua_isboolean(state, -1)) {
	res = lua_toboolean(state, -1);
	lua_pop(state, 1);

        /* Shall we stop the emission of this signal? */
	if (res) {
	  *stop_emission = TRUE;
	}
      }
      break;
    case 0:
      break;
    default:
      lua_pop(state, nres);
      break;
  }
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

    vars = secfile_lookup_str_default(file, "", "script.vars");
    lua_dostring(state, vars);
  }
}

/**************************************************************************
  Save the game script variables to file.
**************************************************************************/
static void script_vars_save(struct section_file *file)
{
  if (state) {
    lua_getglobal(state, "_freeciv_state_dump");
    if (lua_pcall(state, 0, 1, 0) == 0) {
      const char *vars;

      vars = lua_tostring(state, -1);
      lua_pop(state, 1);

      if (vars) {
	secfile_insert_str(file, vars, "script.vars");
      }
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

    code = secfile_lookup_str_default(file, "", "script.code");
    script_code = mystrdup(code);
    lua_dostring(state, script_code);
  }
}

/**************************************************************************
  Save the optional game script code to file (useful for scenarios).
**************************************************************************/
static void script_code_save(struct section_file *file)
{
  if (script_code) {
    secfile_insert_str(file, script_code, "script.code");
  }
}

/**************************************************************************
  Initialize the scripting state.
**************************************************************************/
bool script_init(void)
{
  if (!state) {
    state = lua_open();
    if (!state) {
      return FALSE;
    }

    luaopen_base(state);
    luaopen_string(state);
    luaopen_io(state);
    luaopen_debug(state);
    luaopen_table(state);

    tolua_api_open(state);

    script_code_init();
    script_vars_init();

    script_signals_init();

    return TRUE;
  } else {
    return FALSE;
  }
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

    lua_close(state);
    state = NULL;
  }
}

/**************************************************************************
  Parse and execute the script at filename.
**************************************************************************/
bool script_do_file(const char *filename)
{
  return (lua_dofile(state, filename) == 0);
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

  /* Signals must be loaded last,
   * so we restore their state properly */
  script_signals_load(file);
}

/**************************************************************************
  Save the scripting state to file.
**************************************************************************/
void script_state_save(struct section_file *file)
{
  script_code_save(file);
  script_vars_save(file);

  script_signals_save(file);
}

