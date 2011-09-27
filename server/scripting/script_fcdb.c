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
#include "string_vector.h"

/* common/scriptcore */
#include "luascript.h"
#include "luascript_types.h"
#include "tolua_common_a_gen.h"
#include "tolua_common_z_gen.h"

/* server */
#include "console.h"
#include "stdinhand.h"

/* server/scripting */
#ifdef HAVE_FCDB
#include "tolua_fcdb_gen.h"
#endif /* HAVE_FCDB */

#include "script_fcdb.h"

#ifdef HAVE_FCDB

#define SCRIPT_FCDB_LUA_FILE "database.lua"

static void script_fcdb_functions_define(void);
static bool script_fcdb_functions_check(const char *fcdb_luafile);

static void script_fcdb_cmd_reply(struct fc_lua *fcl, enum log_level level,
                                  const char *format, ...)
            fc__attribute((__format__ (__printf__, 3, 4)));

/*****************************************************************************
  Lua virtual machine state.
*****************************************************************************/
static struct fc_lua *fcl = NULL;

/*****************************************************************************
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
*****************************************************************************/
static void script_fcdb_functions_define(void)
{
  luascript_func_add(fcl, "database_init", TRUE, 0);
  luascript_func_add(fcl, "database_free", TRUE, 0);

  luascript_func_add(fcl, "user_load", TRUE, 1,
                     API_TYPE_CONNECTION);
  luascript_func_add(fcl, "user_save", TRUE, 1,
                     API_TYPE_CONNECTION);
  luascript_func_add(fcl, "user_log", TRUE, 2,
                     API_TYPE_CONNECTION, API_TYPE_BOOL);
}

/*****************************************************************************
  Check the existence of all needed functions.
*****************************************************************************/
static bool script_fcdb_functions_check(const char *fcdb_luafile)
{
  bool ret = TRUE;
  struct strvec *missing_func_required = strvec_new();
  struct strvec *missing_func_optional = strvec_new();

  if (!luascript_func_check(fcl, missing_func_required,
                            missing_func_optional)) {
    strvec_iterate(missing_func_required, func_name) {
      log_error("Database script '%s' does not define the required function "
                "'%s'.", fcdb_luafile, func_name);
      ret = FALSE;
    } strvec_iterate_end;
    strvec_iterate(missing_func_optional, func_name) {
      log_verbose("Database script '%s' does not define the optional "
                  "function '%s'.", fcdb_luafile, func_name);
    } strvec_iterate_end;
  }

  strvec_destroy(missing_func_required);
  strvec_destroy(missing_func_optional);

  return ret;
}

/*****************************************************************************
  Send the message via cmd_reply().
*****************************************************************************/
static void script_fcdb_cmd_reply(struct fc_lua *fcl, enum log_level level,
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

  cmd_reply(CMD_FCDB, fcl->caller, rfc_status, "%s", buf);
}
#endif /* HAVE_FCDB */

/*****************************************************************************
  Initialize the scripting state. Returns the status of the freeciv database
  lua state.
*****************************************************************************/
bool script_fcdb_init(const char *fcdb_luafile)
{
#ifdef HAVE_FCDB
  if (fcl != NULL) {
    fc_assert_ret_val(fcl->state != NULL, FALSE);

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

  fcl = luascript_new(NULL);
  if (!fcl) {
    luascript_destroy(fcl);
    fcl = NULL;

    log_error("Error loading the freeciv database lua definition.");
    return FALSE;
  }

  /* Set the logging output function. */
  fcl->output_fct = script_fcdb_cmd_reply;

  tolua_common_a_open(fcl->state);
  tolua_fcdb_open(fcl->state);
#ifdef HAVE_FCDB_MYSQL
  luaopen_luasql_mysql(fcl->state);
#endif
#ifdef HAVE_FCDB_POSTGRES
  luaopen_luasql_postgres(fcl->state);
#endif
#ifdef HAVE_FCDB_SQLITE3
  luaopen_luasql_sqlite3(fcl->state);
#endif
  tolua_common_z_open(fcl->state);

  luascript_func_init(fcl);

  /* Define the prototypes for the needed lua functions. */
  script_fcdb_functions_define();

  if (luascript_do_file(fcl, fcdb_luafile)
      || !script_fcdb_functions_check(fcdb_luafile)) {
    log_error("Error loading the freeciv database lua script '%s'.",
              fcdb_luafile);
    script_fcdb_free();
    return FALSE;
  }

  if (script_fcdb_call("database_init", 0) != FCDB_SUCCESS_TRUE) {
    log_error("Error connecting to the database");
    script_fcdb_free();
    return FALSE;
  }
#endif /* HAVE_FCDB */

  return TRUE;
}

/*****************************************************************************
  Call a lua function.

  Example call to the lua function 'user_load()':
    script_fcdb_call("user_load", 1, API_TYPE_CONNECTION, pconn);
*****************************************************************************/
enum fcdb_status script_fcdb_call(const char *func_name, int nargs, ...)
{
#ifdef HAVE_FCDB
  enum fcdb_status status = FCDB_ERROR; /* Default return value. */
  bool success;
  int ret;

  va_list args;
  va_start(args, nargs);
  success = luascript_func_call_valist(fcl, func_name, &ret, nargs, args);
  va_end(args);

  if (success && fcdb_status_is_valid(ret)) {
    status = (enum fcdb_status) ret;
  }

  return status;
#else
  return FCDB_SUCCESS_TRUE;
#endif /* HAVE_FCDB */
}

/*****************************************************************************
  Free the scripting data.
*****************************************************************************/
void script_fcdb_free(void)
{
#ifdef HAVE_FCDB
  if (script_fcdb_call("database_free", 0) != FCDB_SUCCESS_TRUE) {
    log_error("Error closing the database connection. Continuing anyway ...");
  }

  if (fcl) {
    /* luascript_func_free() is called by luascript_destroy(). */
    luascript_destroy(fcl);
    fcl = NULL;
  }
#endif /* HAVE_FCDB */
}

/*****************************************************************************
  Parse and execute the script in str in the lua instance for the freeciv
  database.
*****************************************************************************/
bool script_fcdb_do_string(const char *str)
{
#ifdef HAVE_FCDB
  int status;

  status = luascript_do_string(fcl, str, "cmd");
  return (status == 0);
#else
  return TRUE;
#endif /* HAVE_FCDB */
}
