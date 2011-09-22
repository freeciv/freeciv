/****************************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

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

/* dependencies/luasql */
#ifdef HAVE_FCDB_MYSQL
#include "ls_mysql.h"
#endif
#ifdef HAVE_FCDB_POSTGRES
#include "ls_postgres.h"
#endif
#ifdef HAVE_FCDB_SQLITE3
#include "ls_sqlite3.h"
#endif

/* utility */
#include "log.h"
#include "md5.h"
#include "registry.h"

/* server/scripting */
#include "luascript.h"
#include "luascript_types.h"
#include "tolua_common_a_gen.h"
#include "tolua_common_z_gen.h"
#ifdef HAVE_FCDB
#include "tolua_fcdb_gen.h"
#endif /* HAVE_FCDB */

#include "script_fcdb.h"

#ifdef HAVE_FCDB

#define SCRIPT_FCDB_LUA_FILE "database.lua"

static struct fcdb_func *fcdb_func_new(int nargs, enum api_types *parg_types);
static void fcdb_func_destroy(struct fcdb_func *pfcdb_func);

static void script_fcdb_functions_define(void);
static bool script_fcdb_functions_check(const char *fcdb_luafile);
static void script_fcdb_add_func_valist(const char *func_name, int nargs,
                                        va_list args);
static void script_fcdb_add_func(const char *func_name, int nargs, ...);

/****************************************************************************
  Lua virtual machine state.
****************************************************************************/
static lua_State *state = NULL;

struct fcdb_func {
  int nargs;                  /* number of arguments to pass */
  enum api_types *arg_types;  /* argument types */
};

#define SPECHASH_TAG fcdb_func
#define SPECHASH_KEY_TYPE char *
#define SPECHASH_DATA_TYPE struct fcdb_func *
#define SPECHASH_KEY_VAL genhash_str_val_func
#define SPECHASH_KEY_COMP genhash_str_comp_func
#define SPECHASH_KEY_COPY genhash_str_copy_func
#define SPECHASH_KEY_FREE genhash_str_free_func
#define SPECHASH_DATA_FREE fcdb_func_destroy
#include "spechash.h"

#define fcdb_func_hash_keys_iterate(phash, key)                              \
  TYPED_HASH_KEYS_ITERATE(char *, phash, key)
#define fcdb_func_hash_keys_iterate_end                                      \
  HASH_KEYS_ITERATE_END

static struct fcdb_func_hash *fcdb_funcs = NULL;
#endif /* HAVE_FCDB */

/****************************************************************************
  Internal api error function.
  Invoking this will cause Lua to stop executing the current context and
  throw an exception, so to speak.
****************************************************************************/
int script_fcdb_error(const char *format, ...)
{
#ifdef HAVE_FCDB
  va_list vargs;
  int ret;

  va_start(vargs, format);
  ret = luascript_error(state, format, vargs);
  va_end(vargs);

  return ret;
#else
  return -1;
#endif /* HAVE_FCDB */
}

/****************************************************************************
  Like script_error, but using a prefix identifying the called lua function:
    bad argument #narg to '<func>': msg
****************************************************************************/
int script_fcdb_arg_error(int narg, const char *msg)
{
#ifdef HAVE_FCDB
  return luascript_arg_error(state, narg, msg);
#else
  return -1;
#endif /* HAVE_FCDB */
}

#ifdef HAVE_FCDB
/****************************************************************************
  Create a new signal.
****************************************************************************/
static struct fcdb_func *fcdb_func_new(int nargs, enum api_types *parg_types)
{
  struct fcdb_func *pfcdb_func = fc_malloc(sizeof(*pfcdb_func));

  pfcdb_func->nargs = nargs;
  pfcdb_func->arg_types = parg_types;

  return pfcdb_func;
}

/****************************************************************************
  Free a signal.
****************************************************************************/
static void fcdb_func_destroy(struct fcdb_func *pfcdb_func)
{
  if (pfcdb_func->arg_types) {
    free(pfcdb_func->arg_types);
  }
  free(pfcdb_func);
}

/****************************************************************************
  Add fcdb callback functions; these must be defined in the lua script
  'database.lua':

  database_init:
    - test and initialise the database.
  database_free:
    - free the database.

  user_load(Connection pconn):
    - check if the user data was successful loaded from the database.
  user_save(Connection pconn):
    - check if the user data was successful saved in the database.
  user_log(Connection pconn, Bool success):
    - check if the login attempt was successful logged.

  If a database error did occur, the functions return FCDB_SUCCESS_ERROR.
  If the request was successful, FCDB_SUCCESS_TRUE is returned.
  If the request was not successful, FCDB_SUCCESS_FALSE is returned.
****************************************************************************/
static void script_fcdb_functions_define(void)
{
  script_fcdb_add_func("database_init", 0);
  script_fcdb_add_func("database_free", 0);

  script_fcdb_add_func("user_load", 1, API_TYPE_CONNECTION);
  script_fcdb_add_func("user_save", 1, API_TYPE_CONNECTION);
  script_fcdb_add_func("user_log", 2, API_TYPE_CONNECTION, API_TYPE_BOOL);
}

/****************************************************************************
  ...
****************************************************************************/
static bool script_fcdb_functions_check(const char *fcdb_luafile)
{
  bool ret = TRUE;

  if (!fcdb_funcs) {
    return FALSE;
  }

  fcdb_func_hash_keys_iterate(fcdb_funcs, func_name) {
    if (!luascript_func_check(state, func_name)) {
      log_error("Database script '%s' does not define a '%s' function.",
                fcdb_luafile, func_name);
      /* We do not return here to print all errors. */
      ret = FALSE;
    }
  } fcdb_func_hash_keys_iterate_end;

  return ret;
}

/****************************************************************************
  ...
****************************************************************************/
static void script_fcdb_add_func_valist(const char *func_name, int nargs,
                                        va_list args)
{
  struct fcdb_func *pfcdb_func;

  if (fcdb_func_hash_lookup(fcdb_funcs, func_name, &pfcdb_func)) {
    log_error("Freeciv database function '%s' was already created.",
              func_name);
  } else {
    enum api_types *parg_types = fc_calloc(nargs, sizeof(*parg_types));
    char *name = fc_strdup(func_name);
    int i;

    for (i = 0; i < nargs; i++) {
      *(parg_types + i) = va_arg(args, int);
    }

    pfcdb_func = fcdb_func_new(nargs, parg_types);

    fcdb_func_hash_insert(fcdb_funcs, name, pfcdb_func);
  }
}

/****************************************************************************
  ...
****************************************************************************/
static void script_fcdb_add_func(const char *func_name, int nargs, ...)
{
  va_list args;

  va_start(args, nargs);
  script_fcdb_add_func_valist(func_name, nargs, args);
  va_end(args);
}
#endif /* HAVE_FCDB */

/****************************************************************************
  Initialize the scripting state. Returns the status of the freeciv database
  lua state.
****************************************************************************/
bool script_fcdb_init(const char *fcdb_luafile)
{
#ifdef HAVE_FCDB
  if (state) {
    /* state is defined. */
    return TRUE;
  }

  if (!fcdb_luafile) {
    /* Use default freeciv database lua file. */
    fcdb_luafile = fileinfoname(get_data_dirs(), SCRIPT_FCDB_LUA_FILE);
  }

  if (!fcdb_luafile) {
    log_error("Freeciv database script '%s' not in data path.", fcdb_luafile);
    return FALSE;
  }

  state = luascript_new();
  if (!state) {
    log_error("Error loading the freeciv database lua definition.");
    return FALSE;
  }

  tolua_common_a_open(state);
  tolua_fcdb_open(state);
#ifdef HAVE_FCDB_MYSQL
  luaopen_luasql_mysql(state);
#endif
#ifdef HAVE_FCDB_POSTGRES
  luaopen_luasql_postgres(state);
#endif
#ifdef HAVE_FCDB_SQLITE3
  luaopen_luasql_sqlite3(state);
#endif
  tolua_common_z_open(state);

  if (!fcdb_funcs) {
    /* Define the prototypes for the needed lua functions. */
    fcdb_funcs = fcdb_func_hash_new();
    script_fcdb_functions_define();
  }

  if (luascript_do_file(state, fcdb_luafile)
      || !script_fcdb_functions_check(fcdb_luafile)) {
    log_error("Error loading the freeciv database lua script '%s'.",
              fcdb_luafile);
    return FALSE;
  }

  if (script_fcdb_call("database_init", 0) != FCDB_SUCCESS_TRUE) {
    log_error("Error connecting to the database");
    return FALSE;
  }
#endif /* HAVE_FCDB */

  return TRUE;
}

/****************************************************************************
  Call a lua function.

  Example call to the lua function 'user_load()':
    script_fcdb_call("user_load", 1, API_TYPE_CONNECTION, pconn);
****************************************************************************/
enum fcdb_status script_fcdb_call(const char *func_name, int nargs, ...)
{
#ifdef HAVE_FCDB
  struct fcdb_func *pfcdb_func;
  va_list args;
  enum fcdb_status status = FCDB_ERROR; /* Default return value. */

  fc_assert_ret_val(fcdb_funcs, FCDB_ERROR);

  if (!fcdb_func_hash_lookup(fcdb_funcs, func_name, &pfcdb_func)) {
    log_error("FCDB function '%s' does not exist, so cannot be invoked.",
              func_name);
    return status;
  }

  if (pfcdb_func->nargs != nargs) {
    log_error("FCDB function '%s' requires %d args, was passed %d on invoke.",
              func_name, pfcdb_func->nargs, nargs);
    return status;
  }

  /* The function name */
  lua_getglobal(state, func_name);

  if (!lua_isfunction(state, -1)) {
    log_error("lua error: Unknown FCDB function '%s'", func_name);
    lua_pop(state, 1);
    return status;
  }

  va_start(args, nargs);
  luascript_push_args(state, nargs, pfcdb_func->arg_types, args);
  va_end(args);

  /* Call the function with nargs arguments, return 1 results */
  if (luascript_call(state, nargs, 1, NULL) == 0) {
    /* Check the return value. */
    if (lua_isnumber(state, -1)) {
      int ret = lua_tonumber(state, -1);

      if (fcdb_status_is_valid(ret)) {
        status = (enum fcdb_status) ret;
      }
    }
    lua_pop(state, 1);   /* pop return value */
  }

  log_verbose("Call to '%s' returned with '%s'.", func_name,
              fcdb_status_name(status));

  return status;
#else
  return FCDB_SUCCESS_TRUE;
#endif /* HAVE_FCDB */
}

/****************************************************************************
  Free the scripting data.
****************************************************************************/
void script_fcdb_free(void)
{
#ifdef HAVE_FCDB
  if (script_fcdb_call("database_free", 0) != FCDB_SUCCESS_TRUE) {
    log_error("Error closing the database connection. Continuing anyway ...");
  }

  if (state) {
    luascript_destroy(state);
    state = NULL;
  }

  if (fcdb_funcs) {
    fcdb_func_hash_destroy(fcdb_funcs);
    fcdb_funcs = NULL;
  }
#endif /* HAVE_FCDB */
}

/*****************************************************************************
  Parse and execute the script in str in the lua instance for the freeciv database.
*****************************************************************************/
bool script_fcdb_do_string(const char *str)
{
#ifdef HAVE_FCDB
  int status = luascript_do_string(state, str, "cmd");
  return (status == 0);
#else
  return TRUE;
#endif /* HAVE_FCDB */
}
